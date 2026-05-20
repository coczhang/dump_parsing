#include "pdb_analyzer.h"

#include "analyzer_backend.h"

CrashReport PdbAnalyzer::analyze(const QString &dumpPath,
                                 const QString &symbolPath,
                                 const QString &sourceRootPath)
{
    return analyzePdbDumpWithDbgHelp(dumpPath, symbolPath, sourceRootPath);
}
