#include "crash_analyzer.h"

#include "dbg_analyzer.h"
#include "i_analyzer.h"
#include "pdb_analyzer.h"

#include <QFileInfo>

#include <memory>

namespace {

std::unique_ptr<IAnalyzer> createAnalyzerStrategy(const QString &symbolPath)
{
#ifdef Q_OS_WIN
    const bool usePdbAnalyzer = QFileInfo(symbolPath).suffix().compare(QStringLiteral("pdb"), Qt::CaseInsensitive) == 0;
    if (usePdbAnalyzer) {
        return std::make_unique<PdbAnalyzer>();
    }

    return std::make_unique<DbgAnalyzer>();
#else
    Q_UNUSED(symbolPath);
    return std::make_unique<PdbAnalyzer>();
#endif
}

} // namespace

CrashReport CrashAnalyzer::analyze(const QString &dumpPath,
                                   const QString &symbolPath,
                                   const QString &sourceRootPath)
{
    std::unique_ptr<IAnalyzer> analyzer = createAnalyzerStrategy(symbolPath);
    return analyzer->analyze(dumpPath, symbolPath, sourceRootPath);
}
