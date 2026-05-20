#ifndef MAIN_WINDOW_H
#define MAIN_WINDOW_H

#include "crash_analyzer.h"

#include <QWidget>

QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE

class QLineEdit;
class QPushButton;
class QCheckBox;
class QTextBrowser;
class BusySpinner;
class AnalysisController;

class MainWindow : public QWidget
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

private slots:
    void browseDumpFile();
    void browseSymbolFile();
    void browseSourceDirectory();
    void openExportDirectory();
    void analyzeCrash();
    void exportReport();
    void updateAnalyzeButton();
    void updateExportButton();

private:
    QWidget *createFileRow(const QString &labelText,
                           QLineEdit **lineEdit,
                           QPushButton **button) const;
    void setReportMessage(const QString &message);

    Ui::MainWindow *ui;
    QLineEdit *dumpPathEdit = nullptr;
    QLineEdit *symbolPathEdit = nullptr;
    QLineEdit *sourceRootEdit = nullptr;
    QLineEdit *exportDirectoryEdit = nullptr;
    QPushButton *browseDumpButton = nullptr;
    QPushButton *browseSymbolButton = nullptr;
    QPushButton *browseSourceButton = nullptr;
    QPushButton *analyzeButton = nullptr;
    QPushButton *openExportDirButton = nullptr;
    QPushButton *exportButton = nullptr;
    QCheckBox *markdownExportCheck = nullptr;
    QCheckBox *pdfExportCheck = nullptr;
    QCheckBox *textExportCheck = nullptr;
    QTextBrowser *reportView = nullptr;
    BusySpinner *reportSpinner = nullptr;
    AnalysisController *analysisController = nullptr;
    CrashReport lastReport;
    bool hasLastReport = false;
};
#endif // MAIN_WINDOW_H
