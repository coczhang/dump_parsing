#ifndef DBG_ANALYZER_H
#define DBG_ANALYZER_H

#include "i_analyzer.h"

class DbgAnalyzer final : public IAnalyzer
{
public:
    CrashReport analyze(const QString &dumpPath,
                        const QString &symbolPath,
                        const QString &sourceRootPath) override;
};

#endif // DBG_ANALYZER_H
