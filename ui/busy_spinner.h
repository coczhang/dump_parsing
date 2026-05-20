#ifndef BUSY_SPINNER_H
#define BUSY_SPINNER_H

#include <QWidget>

class QPaintEvent;
class QTimer;

class BusySpinner final : public QWidget
{
public:
    explicit BusySpinner(QWidget *parent = nullptr);

    void start();
    void stop();

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QTimer *timer = nullptr;
    int phase = 0;
    static constexpr int kLineCount = 12;
};

#endif // BUSY_SPINNER_H
