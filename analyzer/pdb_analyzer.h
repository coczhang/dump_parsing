#ifndef PDB_ANALYZER_H
#define PDB_ANALYZER_H

#include "i_analyzer.h"

class PdbAnalyzer final : public IAnalyzer
{
public:
    CrashReport analyze(const QString &dumpPath,
                        const QString &symbolPath,
                        const QString &sourceRootPath) override;
};

#endif // PDB_ANALYZER_H
