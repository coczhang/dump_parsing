#ifndef REPORT_RENDERER_H
#define REPORT_RENDERER_H

#include "crash_analyzer.h"

#include <QString>

class ReportRenderer
{
public:
    static QString renderHtml(const CrashReport &report);
};

#endif // REPORT_RENDERER_H
