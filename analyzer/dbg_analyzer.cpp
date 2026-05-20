#include "dbg_analyzer.h"

#include "analyzer_backend.h"

CrashReport DbgAnalyzer::analyze(const QString &dumpPath,
                                 const QString &symbolPath,
                                 const QString &sourceRootPath)
{
    return analyzeDbgDumpWithScripts(dumpPath, symbolPath, sourceRootPath);
}
