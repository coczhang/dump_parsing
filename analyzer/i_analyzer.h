#ifndef I_ANALYZER_H
#define I_ANALYZER_H

#include "crash_analyzer.h"

class IAnalyzer
{
public:
    virtual ~IAnalyzer() = default;

    virtual CrashReport analyze(const QString &dumpPath,
                                const QString &symbolPath,
                                const QString &sourceRootPath) = 0;
};

#endif // I_ANALYZER_H
