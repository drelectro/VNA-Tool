#pragma once

#include <QByteArray>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QTextStream>

#include <cstdio>

// Minimal test harness shared by the bench executables.
//
// This project follows the XMC_SDRv2 convention of headless bench executables
// rather than a unit-test framework: a CLI that exits 0 on pass, 1 on failure,
// 2 on usage error, and can emit machine-readable JSON. That keeps the
// edit -> build -> run -> parse-results loop cheap for both humans and agents.
//
// Usage:
//     Bench b("ScpiBench");
//     b.group("framing");
//     b.check("simple CRLF line", got == "hello", QString("got '%1'").arg(got));
//     return b.finish(jsonOutPath);

class Bench
{
public:
    explicit Bench(QString name) : _name(std::move(name)) { _timer.start(); }

    void setVerbose(bool v) { _verbose = v; }

    // Begin a named group. All subsequent checks belong to it.
    void group(const QString& title)
    {
        flushGroup();
        _group = title;
        _groupPassed = 0;
        _groupFailed = 0;
        if (_verbose) {
            out() << "\n=== " << title << " ===\n";
            out().flush();
        }
    }

    // Record one assertion. `detail` is only reported on failure, and should
    // describe the ACTUAL value — "expected" is already implied by the label.
    bool check(const QString& label, bool condition, const QString& detail = QString())
    {
        if (condition) {
            ++_groupPassed;
            ++_totalPassed;
            if (_verbose) {
                out() << "  [pass] " << label << "\n";
                // Flushed per line in verbose mode: the stream is buffered, so
                // a crash mid-run would otherwise discard every check that had
                // already passed and leave nothing to locate the fault with.
                out().flush();
            }
        } else {
            ++_groupFailed;
            ++_totalFailed;
            out() << "  [FAIL] " << _group << " / " << label;
            if (!detail.isEmpty())
                out() << " -- " << detail;
            out() << "\n";

            QJsonObject f;
            f["group"]  = _group;
            f["check"]  = label;
            f["detail"] = detail;
            _failures.append(f);
        }
        return condition;
    }

    // Convenience for comparing byte arrays, which is most of what we do here.
    bool checkEq(const QString& label, const QByteArray& actual, const QByteArray& expected)
    {
        return check(label, actual == expected,
                     QStringLiteral("got '%1' (%2 bytes), expected '%3' (%4 bytes)")
                         .arg(QString::fromLatin1(actual.toPercentEncoding(" ,.*?\"")))
                         .arg(actual.size())
                         .arg(QString::fromLatin1(expected.toPercentEncoding(" ,.*?\"")))
                         .arg(expected.size()));
    }

    bool checkEq(const QString& label, int actual, int expected)
    {
        return check(label, actual == expected,
                     QStringLiteral("got %1, expected %2").arg(actual).arg(expected));
    }

    bool checkEq(const QString& label, const QString& actual, const QString& expected)
    {
        return check(label, actual == expected,
                     QStringLiteral("got '%1', expected '%2'").arg(actual, expected));
    }

    bool checkNear(const QString& label, double actual, double expected, double tolerance)
    {
        const double d = actual - expected;
        return check(label, (d < 0 ? -d : d) <= tolerance,
                     QStringLiteral("got %1, expected %2 +/- %3")
                         .arg(actual).arg(expected).arg(tolerance));
    }

    int totalPassed() const { return _totalPassed; }
    int totalFailed() const { return _totalFailed; }

    // Prints the summary, optionally writes JSON, and returns the process exit
    // code (0 = all passed, 1 = at least one failure).
    int finish(const QString& jsonPath = QString())
    {
        flushGroup();

        const qint64 elapsedMs = _timer.elapsed();
        out() << "\n" << _name << ": " << _totalPassed << " passed, "
              << _totalFailed << " failed, " << elapsedMs << " ms\n";
        out().flush();

        if (!jsonPath.isEmpty())
            writeJson(jsonPath, elapsedMs);

        return _totalFailed == 0 ? 0 : 1;
    }

private:
    void flushGroup()
    {
        if (_group.isEmpty())
            return;
        QJsonObject g;
        g["name"]   = _group;
        g["passed"] = _groupPassed;
        g["failed"] = _groupFailed;
        _groups.append(g);
        _group.clear();
    }

    inline void writeJson(const QString& path, qint64 elapsedMs);

    static QTextStream& out()
    {
        static QTextStream s(stdout);
        return s;
    }

    QString       _name;
    QString       _group;
    bool          _verbose     = false;
    int           _groupPassed = 0;
    int           _groupFailed = 0;
    int           _totalPassed = 0;
    int           _totalFailed = 0;
    QJsonArray    _groups;
    QJsonArray    _failures;
    QElapsedTimer _timer;
};

inline void Bench::writeJson(const QString& path, qint64 elapsedMs)
{
    QJsonObject root;
    root["bench"]     = _name;
    root["passed"]    = _totalPassed;
    root["failed"]    = _totalFailed;
    root["elapsedMs"] = static_cast<double>(elapsedMs);
    root["groups"]    = _groups;
    root["failures"]  = _failures;

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        out() << "WARNING: could not write " << path << ": " << f.errorString() << "\n";
        return;
    }
    f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    f.close();
    out() << "Wrote " << path << "\n";
}
