#ifndef HARDWARECONTROLLER_H
#define HARDWARECONTROLLER_H

#include <QObject>
#include <QString>

class HardwareController : public QObject {
    Q_OBJECT
    Q_PROPERTY(float topTemp READ topTemp NOTIFY tempsUpdated)
    Q_PROPERTY(float bottomTemp READ bottomTemp NOTIFY tempsUpdated)

public:
    explicit HardwareController(QObject *parent = nullptr);
    ~HardwareController();

    float topTemp() const;
    float bottomTemp() const;

public slots:
    void trayIn();
    void trayOut();
    void startHeaters();
    void stopHeaters();
    void startOptical(int targetCycles);
    void stopOptical();
    void setTopTemp(float temp);
    void setBottomTemp(float temp);

signals:
    void tempsUpdated();
    void graphDataUpdated(int spot, int cycle, float rfu);
    void runComplete(QString message);

private:
    float m_topTemp = 0.0;
    float m_bottomTemp = 0.0;
    static void* serialReaderThread(void* arg);
    static void* opticalThreadFunc(void* arg);
};

#endif
