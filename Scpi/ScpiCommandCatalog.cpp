#include "ScpiCommandCatalog.h"

namespace ScpiCommandCatalog {

QString provenanceName(ScpiProvenance p)
{
    switch (p) {
    case ScpiProvenance::Observed: return QStringLiteral("observed on the instrument");
    case ScpiProvenance::Listed:   return QStringLiteral("listed by *LST?, not exercised");
    case ScpiProvenance::Manual:   return QStringLiteral("from the manual");
    }
    return QString();
}

const QList<ScpiCatalogEntry>& entries()
{
    using P = ScpiProvenance;
    static const QList<ScpiCatalogEntry> k = {
        {"*IDN?", "Manufacturer, model, serial, service version", P::Observed},
        {"*RST", "Restore defaults for the current mode", P::Manual},
        {"*OPC?", "Returns 1", P::Observed},
        {"*ESR?", "Standard event status register (bits undefined)", P::Observed},
        {"*LST?", "List of supported commands, as a block", P::Observed},
        {"DEVice:CONNect?", "Serial of the connected RF unit, or SERVER_NOT_READY", P::Observed},
        {"CLIENT:INFo?", "Model, serial, VNStudio version", P::Observed},
        {"CLIENT:SETUP:SAVE <file>", "Save state to a path on the instrument (.vnaz)", P::Manual},
        {"CLIENT:SETUP:LOAD? <file>", "Load state from a path on the instrument; TRUE or an error", P::Manual},
        {"VNA:CALibration:LOAD? <file>", "Load a calibration file on the instrument; FALSE if not found", P::Observed},
        {"DEVice:CONFiguration:PLLSETTlingdelay <us>", "PLL settling delay, 10-255 us", P::Manual},
        {"DEVice:CONFiguration:PLLSETTlingdelay?", "PLL settling delay", P::Observed},
        {"DEVice:REFerence:IN <INT|EXT>", "Reference clock source", P::Manual},
        {"DEVice:REFerence:IN?", "Reference clock source", P::Observed},
        {"DEVice:STAtus:UNLOcked?", "TRUE if the PLL is unlocked", P::Observed},
        {"DEVice:STAtus:ADCOVERload?", "TRUE on ADC overload", P::Observed},
        {"DEVice:STAtus:UNLEVel?", "TRUE if output level is abnormal", P::Observed},
        {"DEVice:INFo:FWREVision?", "Firmware version", P::Observed},
        {"DEVice:INFo:HWREVision?", "Hardware version", P::Observed},
        {"DEVice:INFo:LIMits:MINFrequency?", "Lowest frequency, Hz", P::Observed},
        {"DEVice:INFo:LIMits:MAXFrequency?", "Highest frequency, Hz", P::Observed},
        {"DEVice:INFo:LIMits:MINIFBW?", "Narrowest IF bandwidth, Hz", P::Observed},
        {"DEVice:INFo:LIMits:MAXIFBW?", "Widest IF bandwidth, Hz", P::Observed},
        {"DEVice:INFo:LIMits:MAXPoints?", "Most points per sweep", P::Observed},
        {"DEVice:INFo:LIMits:MINPOWer?", "Lowest source power, dBm", P::Observed},
        {"DEVice:INFo:LIMits:MAXPOWer?", "Highest source power, dBm", P::Observed},
        {"DEVice:INFo:TEMPeratures?", "RF unit temperature, deg C", P::Observed},
        {"VNA:SWEEP <FREQUENCY|POWER|CW>", "Sweep mode", P::Manual},
        {"VNA:SWEEP?", "Sweep mode", P::Observed},
        {"VNA:SWEEPTYPE <LIN|LOG>", "Frequency sweep spacing", P::Manual},
        {"VNA:SWEEPTYPE?", "Frequency sweep spacing", P::Observed},
        {"VNA:FREQuency:START <Hz>", "Start frequency; no unit suffix", P::Observed},
        {"VNA:FREQuency:START?", "Start frequency", P::Observed},
        {"VNA:FREQuency:STOP <Hz>", "Stop frequency; no unit suffix", P::Manual},
        {"VNA:FREQuency:STOP?", "Stop frequency", P::Observed},
        {"VNA:POWer:START <dBm>", "Power sweep start", P::Manual},
        {"VNA:POWer:START?", "Power sweep start", P::Observed},
        {"VNA:POWer:STOP <dBm>", "Power sweep stop", P::Manual},
        {"VNA:POWer:STOP?", "Power sweep stop", P::Observed},
        {"VNA:STIMulus:LVL <dBm>", "Source power for frequency sweeps", P::Manual},
        {"VNA:STIMulus:LVL?", "Source power", P::Observed},
        {"VNA:STIMulus:FREQuency <Hz>", "Fixed frequency for power sweeps", P::Manual},
        {"VNA:STIMulus:FREQuency?", "Fixed frequency for power sweeps", P::Observed},
        {"VNA:ACQuisition:IFBW <Hz>", "IF bandwidth; out-of-range is rejected", P::Observed},
        {"VNA:ACQuisition:IFBW?", "IF bandwidth", P::Observed},
        {"VNA:ACQuisition:DWELLtime <s>", "Dwell time per point", P::Manual},
        {"VNA:ACQuisition:DWELLtime?", "Dwell time per point", P::Observed},
        {"VNA:ACQuisition:POINTS <n>", "Points per sweep; out-of-range is rejected", P::Observed},
        {"VNA:ACQuisition:POINTS?", "Points per sweep", P::Observed},
        {"VNA:ACQuisition:AVG <n>", "Averaging count on the instrument, 1-99", P::Manual},
        {"VNA:ACQuisition:AVG?", "Averaging count", P::Observed},
        {"VNA:ACQuisition:RUN", "Enable acquisition", P::Observed},
        {"VNA:ACQuisition:RUN?", "TRUE while acquisition is enabled (not 'a sweep is in progress')", P::Observed},
        {"VNA:ACQuisition:STOP", "Disable acquisition", P::Observed},
        {"VNA:ACQuisition:SINGLE <TRUE|FALSE>", "TRUE: single mode and trigger one sweep; FALSE: continuous", P::Observed},
        {"VNA:ACQuisition:SINGLE?", "Single mode", P::Observed},
        {"VNA:ACQuisition:FREQuency?", "Current sweep position, Hz", P::Observed},
        {"VNA:ACQuisition:POWer?", "Current sweep position, dBm", P::Listed},
        {"VNA:ACQuisition:TIME?", "Current sweep position, s (CW mode)", P::Listed},
        {"VNA:ACQuisition:SWEep:TIME?", "Sweep time for all enabled traces, s", P::Observed},
        {"VNA:ACQuisition:SWEep:SPEed?", "Points per second", P::Observed},
        {"VNA:ACQuisition:SWEep:VALID?", "FALSE right after SINGLE TRUE, otherwise TRUE", P::Observed},
        {"VNA:ACQuisition:PHASeflip:ENABle <ON|OFF>", "Experimental phase-flip correction", P::Listed},
        {"VNA:ACQuisition:PHASeflip:ENABle?", "Phase-flip correction state", P::Observed},
        {"VNA:ACQuisition:PHASeflip:STATus?", "Phase-flip status as JSON", P::Observed},
        {"VNA:ACQuisition:RAWRx <TRUE|FALSE>", "Enable raw receiver frames", P::Observed},
        {"VNA:ACQuisition:RAWRx?", "Raw receiver frames enabled", P::Observed},
        {"VNA:ACQuisition:RAWRx:FRAME?", "Latest raw receiver frame (VRF1 block), or NO_DATA", P::Observed},
        {"VNA:ACQuisition:GROups:COUNt <n>", "Sweeps per group", P::Observed},
        {"VNA:ACQuisition:GROups:COUNt?", "Sweeps per group", P::Observed},
        {"VNA:ACQuisition:GROups:PERiod", "Group period", P::Listed},
        {"VNA:ACQuisition:GROups:PERiod?", "Group period; 0,0 observed", P::Observed},
        {"VNA:ACQuisition:GROups:CONTinuous <ON|OFF>", "ON arms a group at once; the query stays OFF and nothing re-arms", P::Observed},
        {"VNA:ACQuisition:GROups:CONTinuous?", "Continuous groups; OFF observed", P::Observed},
        {"VNA:ACQuisition:GROups:INITiate", "Capture the next group of sweeps", P::Observed},
        {"VNA:ACQuisition:GROups:STATus?", "state,groupId,done,total,reason", P::Observed},
        {"VNA:ACQuisition:GROups:ABORt", "Abort a running group; error when idle", P::Observed},
        {"VNA:TRACe:DATA? <S11|S21|S12|S22>", "Complex trace as [x,re,im],...", P::Observed},
        {"VNA:TRACe:GROups:DATA?", "Completed group as a VGB1 block, or GROUP_NOT_READY", P::Observed},
        {"VNA:TRACe:PAUSE <Sxx>", "Accepted; effect not established", P::Observed},
        {"VNA:TRACe:RESUME <Sxx>", "Accepted; effect not established", P::Observed},
        {"VNA:TRACe:PAUSED? <Sxx>", "FALSE observed", P::Observed},
    };
    return k;
}

} // namespace ScpiCommandCatalog
