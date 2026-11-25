// Harness config: CSV loading of Data/Tuning.csv (Name,Value) and Data/Jams.csv (one row per jam).
// Single source of tuning for the harness; the Params struct itself stays plain C++.
#pragma once

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "ShiftParams.h"
#include "policies.h"

namespace ShiftSim
{

struct Config
{
    Params params;
    PolicyParams policy;
    std::vector<JamDef> jams;
    std::string dataDir;
    std::vector<std::string> jamRowNames;
};

struct CsvTable
{
    std::vector<std::string> header;
    std::vector<std::vector<std::string>> rows;
};

inline std::string Trim(const std::string& S)
{
    size_t B = 0;
    size_t E = S.size();
    while (B < E && (S[B] == ' ' || S[B] == '\t' || S[B] == '\r' || S[B] == '\n'))
    {
        ++B;
    }
    while (E > B && (S[E - 1] == ' ' || S[E - 1] == '\t' || S[E - 1] == '\r' || S[E - 1] == '\n'))
    {
        --E;
    }
    return S.substr(B, E - B);
}

inline std::vector<std::string> SplitCsvLine(const std::string& Line)
{
    std::vector<std::string> Out;
    std::string Cur;
    bool Quoted = false;
    for (size_t I = 0; I < Line.size(); ++I)
    {
        const char C = Line[I];
        if (Quoted)
        {
            if (C == '"')
            {
                if (I + 1 < Line.size() && Line[I + 1] == '"')
                {
                    Cur.push_back('"');
                    ++I;
                }
                else
                {
                    Quoted = false;
                }
            }
            else
            {
                Cur.push_back(C);
            }
        }
        else if (C == '"')
        {
            Quoted = true;
        }
        else if (C == ',')
        {
            Out.push_back(Trim(Cur));
            Cur.clear();
        }
        else
        {
            Cur.push_back(C);
        }
    }
    Out.push_back(Trim(Cur));
    return Out;
}

inline bool LoadCsv(const std::string& Path, CsvTable* Out, std::string* Err)
{
    std::ifstream In(Path.c_str());
    if (!In.is_open())
    {
        if (Err != nullptr) { *Err = "cannot open " + Path; }
        return false;
    }
    Out->header.clear();
    Out->rows.clear();
    std::string Line;
    bool First = true;
    while (std::getline(In, Line))
    {
        const std::string T = Trim(Line);
        if (T.empty() || T[0] == '#')
        {
            continue;
        }
        std::vector<std::string> Cells = SplitCsvLine(T);
        if (First)
        {
            Out->header = Cells;
            First = false;
        }
        else
        {
            Out->rows.push_back(Cells);
        }
    }
    return !Out->header.empty();
}

inline bool FileExists(const std::string& Path)
{
    std::ifstream In(Path.c_str());
    return In.is_open();
}

// Walks up from Start looking for "Data/Jams.csv"; returns the Data directory or "".
inline std::string FindDataDirFrom(std::string Start)
{
    for (int Up = 0; Up < 8; ++Up)
    {
        const std::string Cand = Start + "/Data";
        if (FileExists(Cand + "/Jams.csv") || FileExists(Cand + "/Tuning.csv"))
        {
            return Cand;
        }
        const size_t Slash = Start.find_last_of("/\\");
        if (Slash == std::string::npos)
        {
            break;
        }
        Start = Start.substr(0, Slash);
    }
    return "";
}

inline bool ParseDouble(const std::string& S, double* Out)
{
    if (S.empty())
    {
        return false;
    }
    char* End = nullptr;
    const double V = std::strtod(S.c_str(), &End);
    if (End == S.c_str() || *End != '\0')
    {
        return false;
    }
    *Out = V;
    return true;
}

inline bool LoadConfig(const std::string& DataDir, Config* Cfg, std::string* Err)
{
    Cfg->dataDir = DataDir;
    Cfg->jams.clear();
    Cfg->jamRowNames.clear();
    for (int I = 0; I < kJamCount; ++I)
    {
        Cfg->jams.push_back(DefaultJam(I));
        Cfg->jamRowNames.push_back("Jam" + std::to_string(I + 1));
    }
    if (DataDir.empty())
    {
        return true; // built-in defaults
    }

    CsvTable Tuning;
    std::string E;
    if (LoadCsv(DataDir + "/Tuning.csv", &Tuning, &E))
    {
        for (size_t R = 0; R < Tuning.rows.size(); ++R)
        {
            const std::vector<std::string>& Row = Tuning.rows[R];
            double V = 0.0;
            if (Row.size() < 2 || !ParseDouble(Row[1], &V))
            {
                if (Err != nullptr) { *Err = "Tuning.csv: bad row " + std::to_string(R + 2); }
                return false;
            }
            if (!SetParam(Cfg->params, Row[0], V) && !SetPolicyParam(Cfg->policy, Row[0], V))
            {
                if (Err != nullptr) { *Err = "Tuning.csv: unknown name '" + Row[0] + "'"; }
                return false;
            }
        }
    }

    CsvTable Jams;
    if (LoadCsv(DataDir + "/Jams.csv", &Jams, &E))
    {
        Cfg->jams.clear();
        Cfg->jamRowNames.clear();
        for (size_t R = 0; R < Jams.rows.size(); ++R)
        {
            JamDef J;
            std::string RowName = "Jam" + std::to_string(R + 1);
            const std::vector<std::string>& Row = Jams.rows[R];
            for (size_t C = 0; C < Jams.header.size() && C < Row.size(); ++C)
            {
                const std::string& H = Jams.header[C];
                if (H == "Name")
                {
                    RowName = Row[C];
                    continue;
                }
                if (H == "Title")
                {
                    SetJamName(J, Row[C]);
                    continue;
                }
                double V = 0.0;
                if (!ParseDouble(Row[C], &V) || !SetJamField(J, H, V))
                {
                    if (Err != nullptr) { *Err = "Jams.csv: bad column/value '" + H + "' row " + std::to_string(R + 2); }
                    return false;
                }
            }
            Cfg->jamRowNames.push_back(RowName);
            Cfg->jams.push_back(J);
        }
    }
    return true;
}

// The Params a jam is played with: the jam's own tweaks (ApplyJamTuning, none for jams 0..2) on the compile-time defaults, and on top of that whatever Tuning.csv or a
// -Set changed in Cfg.params (an explicit value wins over a jam's tweak, as QA names do in the game).
inline Params ParamsFor(const Config& Cfg, int Jam)
{
    const Params Defaults;
    Params P = Defaults;
    ApplyJamTuning(Jam, P);
    for (int I = 0; I < ParamCount(); ++I)
    {
        double Have = 0.0;
        double Base = 0.0;
        if (GetParam(Cfg.params, ParamNameAt(I), &Have) && GetParam(Defaults, ParamNameAt(I), &Base) && Have != Base)
        {
            SetParam(P, ParamNameAt(I), Have);
        }
    }
    return P;
}

inline std::string FmtNum(double V)
{
    char Buf[64];
    std::snprintf(Buf, sizeof(Buf), "%.10g", V);
    return std::string(Buf);
}

inline bool WriteTuningCsv(const std::string& Path, const Params& P, const PolicyParams& PP)
{
    std::ofstream Out(Path.c_str());
    if (!Out.is_open())
    {
        return false;
    }
    Out << "Name,Value\n";
    for (int I = 0; I < ParamCount(); ++I)
    {
        double V = 0.0;
        GetParam(P, ParamNameAt(I), &V);
        Out << ParamNameAt(I) << "," << FmtNum(V) << "\n";
    }
    for (int I = 0; I < PolicyParamCount(); ++I)
    {
        double V = 0.0;
        GetPolicyParam(PP, PolicyParamNameAt(I), &V);
        Out << PolicyParamNameAt(I) << "," << FmtNum(V) << "\n";
    }
    return true;
}

inline bool WriteJamsCsv(const std::string& Path, const std::vector<JamDef>& Jams)
{
    std::ofstream Out(Path.c_str());
    if (!Out.is_open())
    {
        return false;
    }
    Out << "Name,Title,Nodes,Communities,Bridges,Celebs,Entries,Days,SecPerDay,Regen,LocalGate,CelebGate,RivalId,"
           "CelebExtraFollowers,RivalFollowerFraction,RivalInterval,ModIntervalMin,ModIntervalMax\n";
    for (size_t I = 0; I < Jams.size(); ++I)
    {
        const JamDef& J = Jams[I];
        Out << "Jam" << (I + 1) << "," << J.name << "," << J.nodes << "," << J.communities << "," << J.bridges << ","
            << J.celebs << "," << J.entries << "," << J.days << "," << FmtNum(J.secPerDay) << "," << FmtNum(J.regen)
            << "," << FmtNum(J.localGate) << "," << FmtNum(J.celebGate) << "," << J.rivalId << ","
            << FmtNum(J.celebExtraFollowers) << "," << FmtNum(J.rivalFollowerFraction) << "," << FmtNum(J.rivalInterval) << ","
            << FmtNum(J.modIntervalMin) << "," << FmtNum(J.modIntervalMax) << "\n";
    }
    return true;
}

} // namespace ShiftSim
