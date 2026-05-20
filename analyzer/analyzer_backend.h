#ifndef ANALYZER_BACKEND_H
#define ANALYZER_BACKEND_H

#include "crash_analyzer.h"

CrashReport analyzePdbDumpWithDbgHelp(const QString &dumpPath,
                                      const QString &symbolPath,
                                      const QString &sourceRootPath);

CrashReport analyzeDbgDumpWithScripts(const QString &dumpPath,
                                      const QString &symbolPath,
                                      const QString &sourceRootPath);

#endif // ANALYZER_BACKEND_H
