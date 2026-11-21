#include <QApplication>
#include <QMainWindow>
#include <QStackedWidget>
#include <QPushButton>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QMessageBox>
#include <QTimer>
#include <QDateTime>
#include <QThread>
#include <QtCharts/QChartView>
#include <QtCharts/QLineSeries>
#include <QtCharts/QValueAxis>

// Standard C Includes for Hardware
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>
#include <atomic>
#include <fcntl.h>
#include <termios.h>
#include <sys/stat.h>
#include <time.h>
#include <lgpio.h>
#include <curl/curl.h>
#include <thread>

QT_CHARTS_USE_NAMESPACE

// --- Hardware Config ---
#define LIMIT_SWITCH_PIN   6 
#define LIMIT_SWITCH_2_PIN 5  
#define POWER_12V_PIN      17
#define LED_PIN            13
#define MOTOR1_DIR_PIN     20 
#define MOTOR1_STEP_PIN    21
#define MOTOR1_ENABLE_PIN  16
#define MOTOR2_DIR_PIN     24
#define MOTOR2_STEP_PIN    18
#define MOTOR2_ENABLE_PIN  4
#define I2C_BUS          1
#define ADS1115_ADDR     0x48

#define SERIAL_PORT "/dev/ttyACM0" 

int gpio_handle;
int i2c_handle;
int serial_fd = -1; 

std::atomic<bool> optical_running(false);
std::atomic<bool> optical_stop_requested(false);

float temp_data[4] = {0.0, 0.0, 0.0, 0.0};
pthread_mutex_t temp_mutex = PTHREAD_MUTEX_INITIALIZER;
int total_cycles = 60; 
int top_setpoint = 64;
int bot_setpoint = 65;
char current_disease[32] = ""; // FIXED: Added missing variable back!

void gui_show_notification(const QString& title, const QString& msg);
void gui_add_graph_point(int cycle, int spot, int adc_val);

// =======================================================
// ACTUAL MOTOR CONTROL (WITH ACCELERATION TO PREVENT HUMMING)
// =======================================================
void drv8825_enable(int motor, int state) { 
    int pin = (motor == 1) ? MOTOR1_ENABLE_PIN : MOTOR2_ENABLE_PIN;
    lgGpioWrite(gpio_handle, pin, state ? 0 : 1); 
}

void drv8825_turn(int motor, int steps, int dir, int delay_us) { 
    int dir_pin = (motor == 1) ? MOTOR1_DIR_PIN : MOTOR2_DIR_PIN;
    int step_pin = (motor == 1) ? MOTOR1_STEP_PIN : MOTOR2_STEP_PIN;
    
    lgGpioWrite(gpio_handle, dir_pin, dir);
    usleep(50); // Let DIR pin settle
    
    for(int i = 0; i < steps; i++) {
        if(motor == 1 && optical_stop_requested) break;
        
        // ACCELERATION RAMP: Start slow, ramp up to prevent stalling
        int current_delay = delay_us;
        if (i < 200) { current_delay = delay_us + (200 - i) * 5; }

        lgGpioWrite(gpio_handle, step_pin, 1);
        usleep(5); // 5us is perfect for DRV8825 trigger
        lgGpioWrite(gpio_handle, step_pin, 0);
        usleep(current_delay);
    }
}

void drv8825_stop(int motor) { 
    drv8825_enable(motor, 0); 
}

// OPTICAL HOMING (With Acceleration)
void optical_home() {
    lgGpioWrite(gpio_handle, MOTOR1_DIR_PIN, 1); 
    usleep(50);
    int i = 0;
    int target_delay = 600; // Base speed
    
    while (lgGpioRead(gpio_handle, LIMIT_SWITCH_PIN) == 1 && !optical_stop_requested) {
        int current_delay = target_delay;
        if (i < 200) current_delay = target_delay + (200 - i) * 5; // Ramp up
        
        lgGpioWrite(gpio_handle, MOTOR1_STEP_PIN, 1); usleep(5); 
        lgGpioWrite(gpio_handle, MOTOR1_STEP_PIN, 0); usleep(current_delay);
        i++;
    }
    usleep(200000);
}

// --- ADC Helper ---
int read_ads1115(int i2c_h) {
    char config_write[3] = {0x01, (char)0xC6, 0x08};
    lgI2cWriteDevice(i2c_h, config_write, 3);
    usleep(15000);
    char reg_pointer[1] = {0x00};
    lgI2cWriteDevice(i2c_h, reg_pointer, 1);
    char data[2];
    lgI2cReadDevice(i2c_h, data, 2);
    int val = (data[0] << 8) | data[1];
    if (val > 0x7FFF) val -= 0x10000;
    return val;
}

// Thread to read temperatures from Serial Port
void* serial_reader_thread(void* arg) {
    (void)arg;
    char read_buf[256]; char line_buf[512]; int line_len = 0;
    while(1) {
        if(serial_fd != -1) {
            int n = read(serial_fd, read_buf, sizeof(read_buf)-1);
            if (n > 0) {
                read_buf[n] = '\0';
                for(int i = 0; i < n; i++) {
                    if (read_buf[i] == '\n') {
                        line_buf[line_len] = '\0';
                        char *ptr = strstr(line_buf, "A0:");
                        if (ptr) {
                            float t0, t1, t2, t3;
                            if (sscanf(ptr, "A0: %fC | A1: %fC | A2: %fC | A3: %fC", &t0, &t1, &t2, &t3) == 4) {
                                pthread_mutex_lock(&temp_mutex);
                                temp_data[0] = t0; temp_data[1] = t1;
                                temp_data[2] = t2; temp_data[3] = t3;
                                pthread_mutex_unlock(&temp_mutex);
                            }
                        }
                        line_len = 0;
                    } else if (line_len < 511) { line_buf[line_len++] = read_buf[i]; }
                }
            }
        }
        usleep(50000); 
    }
    return NULL;
}

// Optical Thread Routine
void optical_thread_func() {
    optical_stop_requested = false;
    lgGpioWrite(gpio_handle, LED_PIN, 0);
    
    struct timespec cycle_target_time, now;

    for (int cycle = 1; cycle <= total_cycles; cycle++) {
        if(optical_stop_requested) break;
        
        drv8825_enable(1, 1);
        optical_home(); 
        if (cycle == 1) {
            clock_gettime(CLOCK_MONOTONIC, &now);
            cycle_target_time = now;
            cycle_target_time.tv_sec += 60;
        }

        // Dark Current 
        drv8825_turn(1, 1310, 0, 600); 
        lgGpioWrite(gpio_handle, POWER_12V_PIN, 1);
        usleep(100000);
        read_ads1115(i2c_handle);
        optical_home(); 
        
        drv8825_turn(1, 75, 0, 600); // Spot 1
        for (int spot = 1; spot <= 6; spot++) {
            if(optical_stop_requested) break;
            usleep(100000);
            long sum_adc = 0; 
            for (int r = 0; r < 5; r++) {
                lgGpioWrite(gpio_handle, LED_PIN, 1); usleep(150000);
                sum_adc += read_ads1115(i2c_handle);
                lgGpioWrite(gpio_handle, LED_PIN, 0); usleep(150000);
            }
            gui_add_graph_point(cycle, spot, sum_adc / 5);
            if (spot < 6) drv8825_turn(1, 167, 0, 600);
        }
        
        lgGpioWrite(gpio_handle, LED_PIN, 0);
        lgGpioWrite(gpio_handle, POWER_12V_PIN, 0);
        
        if (!optical_stop_requested) {
            optical_home();
            drv8825_stop(1); // Relax motor to prevent heating
            clock_gettime(CLOCK_MONOTONIC, &now);
            long diff_ms = (cycle_target_time.tv_sec - now.tv_sec)*1000;
            while (!optical_stop_requested && diff_ms > 0) {
                usleep(50000);
                clock_gettime(CLOCK_MONOTONIC, &now);
                diff_ms = (cycle_target_time.tv_sec - now.tv_sec)*1000;
            }
            cycle_target_time.tv_sec += 60;
        }
    }
    
    drv8825_stop(1);
    optical_running = false;
    gui_show_notification("Complete", "Optical Run Finished.");
}


// ==========================================
// Qt GUI Implementation
// ==========================================

class NumericKeypad : public QDialog {
public:
    int value = -1;
    NumericKeypad(const QString& title, QWidget *parent = nullptr) : QDialog(parent) {
        setWindowTitle(title);
        setFixedSize(300, 400);
        QVBoxLayout* mainL = new QVBoxLayout(this);
        QLabel* display = new QLabel("0");
        display->setStyleSheet("font-size: 32px; background: white; border: 1px solid black; padding: 5px;");
        display->setAlignment(Qt::AlignRight);
        mainL->addWidget(display);
        
        QGridLayout* grid = new QGridLayout();
        QString buttons[12] = {"7","8","9","4","5","6","1","2","3","C","0","OK"};
        int pos = 0;
        for(int r=0; r<4; r++) {
            for(int c=0; c<3; c++) {
                QPushButton* btn = new QPushButton(buttons[pos]);
                btn->setFixedSize(80, 80);
                btn->setStyleSheet("font-size: 24px; font-weight: bold; background-color: #e0e0e0;");
                grid->addWidget(btn, r, c);
                connect(btn, &QPushButton::pressed, [this, btn, display]() {
                    QString txt = btn->text();
                    if(txt == "C") display->setText("0");
                    else if(txt == "OK") { value = display->text().toInt(); accept(); }
                    else {
                        if(display->text() == "0") display->setText(txt);
                        else display->setText(display->text() + txt);
                    }
                });
                pos++;
            }
        }
        mainL->addLayout(grid);
    }
};

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    QStackedWidget* stack;
    QLabel* lblClock;
    QLabel* lblA2;
    QLabel* lblA3;
    QLabel* lblStatus; 
    QPushButton* btnTopSet;
    QPushButton* btnBotSet;
    QPushButton* btnCycles;
    
    QChart* chart;
    QLineSeries* series[6];
    QValueAxis* axisX;

    MainWindow() {
        setFixedSize(800, 480); 
        setWindowFlags(Qt::FramelessWindowHint); 
        stack = new QStackedWidget(this);
        setCentralWidget(stack);

        buildSplashPage();
        buildModePage();
        buildDiseasePage();
        buildControlPage();
        buildGraphPage();

        lblClock = new QLabel(this);
        lblClock->setGeometry(600, 440, 190, 30);
        lblClock->setStyleSheet("font-size: 16px; font-weight: bold; color: #333;");
        QTimer* clockTimer = new QTimer(this);
        connect(clockTimer, &QTimer::timeout, this, &MainWindow::updateClock);
        clockTimer->start(1000);

        QTimer* tempTimer = new QTimer(this);
        connect(tempTimer, &QTimer::timeout, this, &MainWindow::updateTemps);
        tempTimer->start(1000);

        QTimer::singleShot(5000, this, [this]() { stack->setCurrentIndex(1); });
    }

public slots:
    void updateClock() {
        QDateTime kolkataTime = QDateTime::currentDateTimeUtc().addSecs(5 * 3600 + 1800);
        lblClock->setText("Time: " + kolkataTime.toString("HH:mm:ss"));
    }

    void updateTemps() {
        pthread_mutex_lock(&temp_mutex);
        float a2 = temp_data[2];
        float a3 = temp_data[3];
        pthread_mutex_unlock(&temp_mutex);
        lblA2->setText(QString::number(a2, 'f', 1) + " °C");
        lblA3->setText(QString::number(a3, 'f', 1) + " °C");
    }

    void appendGraphPoint(int cycle, int spot, int adc_val) {
        if(spot >= 1 && spot <= 6) {
            series[spot-1]->append(cycle, adc_val);
            if(cycle > axisX->max()) axisX->setMax(cycle + 10);
        }
    }

private:
    void buildSplashPage() {
        QWidget* p = new QWidget();
        QVBoxLayout* l = new QVBoxLayout(p);
        QLabel* title = new QLabel("PureGene");
        title->setAlignment(Qt::AlignCenter);
        title->setStyleSheet("font-size: 72px; font-weight: bold; color: #2c3e50;");
        l->addWidget(title);
        stack->addWidget(p);
    }

    void buildModePage() {
        QWidget* p = new QWidget();
        QHBoxLayout* l = new QHBoxLayout(p);
        QPushButton* btnCalib = new QPushButton("Calibration Mode");
        QPushButton* btnOperate = new QPushButton("Operation Mode");
        
        QString btnStyle = "QPushButton { font-size: 24px; padding: 20px; background-color: #3498db; color: white; border-radius: 10px; } "
                           "QPushButton:pressed { background-color: #2980b9; }";
        btnCalib->setStyleSheet(btnStyle);
        btnOperate->setStyleSheet(btnStyle);
        
        l->addWidget(btnCalib);
        l->addWidget(btnOperate);
        connect(btnOperate, &QPushButton::pressed, [this]() { stack->setCurrentIndex(2); });
        stack->addWidget(p);
    }

    void buildDiseasePage() {
        QWidget* p = new QWidget();
        QGridLayout* l = new QGridLayout(p);
        QString diseases[] = {"HPV", "HCV", "TB", "Carbapenem"};
        QString btnStyle = "QPushButton { font-size: 28px; background-color: #2ecc71; color: white; border-radius: 15px; } "
                           "QPushButton:pressed { background-color: #27ae60; }";
        
        for(int i=0; i<4; i++) {
            QPushButton* btn = new QPushButton(diseases[i]);
            btn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
            btn->setStyleSheet(btnStyle);
            l->addWidget(btn, i/2, i%2);
            connect(btn, &QPushButton::pressed, [this, name = diseases[i]]() {
                strcpy(current_disease, name.toStdString().c_str());
                stack->setCurrentIndex(3);
            });
        }
        QPushButton* back = new QPushButton("Back");
        back->setStyleSheet("QPushButton { background: #e74c3c; color: white; padding: 10px; } QPushButton:pressed { background: #c0392b; }");
        connect(back, &QPushButton::pressed, [this](){ stack->setCurrentIndex(1); });
        l->addWidget(back, 2, 0, 1, 2);
        stack->addWidget(p); 
    }

    void buildControlPage() {
        QWidget* p = new QWidget();
        QGridLayout* grid = new QGridLayout(p);
        
        QString mainBtnStyle = "QPushButton { font-size: 18px; padding: 15px; background: #34495e; color: white; border-radius: 5px; } "
                               "QPushButton:pressed { background: #e67e22; }"; 
        
        lblStatus = new QLabel("System Ready", p);
        lblStatus->setStyleSheet("font-size: 18px; font-weight: bold; color: #27ae60;");
        
        QPushButton* bTrayIn = new QPushButton("Tray In");
        QPushButton* bTrayOut = new QPushButton("Tray Out");
        bTrayIn->setStyleSheet(mainBtnStyle); bTrayOut->setStyleSheet(mainBtnStyle);
        
        connect(bTrayIn, &QPushButton::pressed, [this]() {
            lblStatus->setText("Action: Homing Tray In...");
            std::thread([this]() {
                drv8825_enable(2, 1);
                lgGpioWrite(gpio_handle, MOTOR2_DIR_PIN, 0); 
                usleep(50);
                int i = 0; int target_delay = 600;
                while (lgGpioRead(gpio_handle, LIMIT_SWITCH_2_PIN) == 1) {
                    int current_delay = target_delay;
                    if (i < 200) current_delay = target_delay + (200 - i) * 5;
                    lgGpioWrite(gpio_handle, MOTOR2_STEP_PIN, 1); usleep(5);
                    lgGpioWrite(gpio_handle, MOTOR2_STEP_PIN, 0); usleep(current_delay);
                    i++;
                }
                drv8825_stop(2);
                QMetaObject::invokeMethod(this, [this](){ lblStatus->setText("Status: Tray In Complete"); }, Qt::QueuedConnection);
            }).detach();
        });
        
        connect(bTrayOut, &QPushButton::pressed, [this]() {
            lblStatus->setText("Action: Moving Tray Out...");
            std::thread([this]() {
                drv8825_enable(2, 1); 
                drv8825_turn(2, 3000, 1, 600); 
                drv8825_stop(2);
                QMetaObject::invokeMethod(this, [this](){ lblStatus->setText("Status: Tray Out Complete"); }, Qt::QueuedConnection);
            }).detach();
        });

        QPushButton* bHeat = new QPushButton("Start Heaters");
        bHeat->setStyleSheet(mainBtnStyle);
        connect(bHeat, &QPushButton::pressed, [this]() {
            if(serial_fd != -1) {
                write(serial_fd, "start heaters\n", 14);
                lblStatus->setText("Action: Heaters Started!");
            } else {
                lblStatus->setText("Error: Arduino Not Connected!");
            }
        });

        btnTopSet = new QPushButton(QString("Top Set: %1").arg(top_setpoint));
        btnBotSet = new QPushButton(QString("Bot Set: %1").arg(bot_setpoint));
        btnTopSet->setStyleSheet(mainBtnStyle); btnBotSet->setStyleSheet(mainBtnStyle);

        lblA2 = new QLabel("0.0 °C"); lblA3 = new QLabel("0.0 °C");
        lblA2->setStyleSheet("font-size: 24px; color: red; font-weight:bold;");
        lblA3->setStyleSheet("font-size: 24px; color: blue; font-weight:bold;");
        
        auto setpointClick = [this](QPushButton* btn, int& valRef, const char* cmdPrefix) {
            NumericKeypad kp("Set Temp", this);
            if(kp.exec() == QDialog::Accepted) {
                valRef = kp.value;
                btn->setText(QString("%1 Set: %2").arg(cmdPrefix).arg(valRef));
                if(serial_fd != -1) {
                    char cmd[64]; snprintf(cmd, sizeof(cmd), "set_%s:%d\n", cmdPrefix, valRef);
                    write(serial_fd, cmd, strlen(cmd));
                }
            }
        };
        connect(btnTopSet, &QPushButton::pressed, [=](){ setpointClick(btnTopSet, top_setpoint, "top"); });
        connect(btnBotSet, &QPushButton::pressed, [=](){ setpointClick(btnBotSet, bot_setpoint, "bot"); });

        btnCycles = new QPushButton(QString("Cycles: %1").arg(total_cycles));
        btnCycles->setStyleSheet(mainBtnStyle);
        connect(btnCycles, &QPushButton::pressed, [=](){
            NumericKeypad kp("Set Cycles", this);
            if(kp.exec() == QDialog::Accepted) {
                total_cycles = kp.value;
                btnCycles->setText(QString("Cycles: %1").arg(total_cycles));
            }
        });

        QPushButton* bOpt = new QPushButton("Start Optical");
        bOpt->setStyleSheet(mainBtnStyle);
        connect(bOpt, &QPushButton::pressed, [this]() {
            if(!optical_running) {
                lblStatus->setText("Action: Optical Sequence Started!");
                optical_running = true; 
                std::thread([]() { optical_thread_func(); }).detach();
            }
        });

        QPushButton* bPneum = new QPushButton("Start Pneumatics");
        bPneum->setStyleSheet(mainBtnStyle);
        connect(bPneum, &QPushButton::pressed, [this]() {
            if(serial_fd != -1) {
                write(serial_fd, "start pneumatics\n", 17);
                lblStatus->setText("Action: Pneumatic Sequence Started!");
            } else {
                lblStatus->setText("Error: Arduino Not Connected!");
            }
        });

        QPushButton* bGraph = new QPushButton("View Graph");
        bGraph->setStyleSheet("QPushButton { font-size: 18px; padding: 15px; background: #8e44ad; color: white; border-radius: 5px; } QPushButton:pressed { background: #9b59b6; }");
        connect(bGraph, &QPushButton::pressed, [this](){ stack->setCurrentIndex(4); });

        QPushButton* back = new QPushButton("Back");
        back->setStyleSheet("QPushButton { background: #e74c3c; color: white; padding: 10px; } QPushButton:pressed { background: #c0392b; }");
        connect(back, &QPushButton::pressed, [this](){ stack->setCurrentIndex(2); });

        grid->addWidget(bTrayIn, 0, 0); grid->addWidget(bTrayOut, 0, 1);
        grid->addWidget(bHeat, 1, 0);   grid->addWidget(bPneum, 1, 1);
        grid->addWidget(btnTopSet, 2, 0); grid->addWidget(lblA2, 2, 1);
        grid->addWidget(btnBotSet, 3, 0); grid->addWidget(lblA3, 3, 1);
        grid->addWidget(btnCycles, 4, 0); grid->addWidget(bOpt, 4, 1);
        grid->addWidget(back, 5, 0);      grid->addWidget(bGraph, 5, 1);
        grid->addWidget(lblStatus, 6, 0, 1, 2); 

        stack->addWidget(p);
    }

    void buildGraphPage() {
        QWidget* p = new QWidget();
        QVBoxLayout* l = new QVBoxLayout(p);
        
        chart = new QChart();
        chart->setTitle("Optical RFU vs Time");
        axisX = new QValueAxis(); axisX->setRange(0, 60); axisX->setTitleText("Cycle");
        QValueAxis* axisY = new QValueAxis(); axisY->setRange(0, 4000); axisY->setTitleText("RFU (ADC)");
        chart->addAxis(axisX, Qt::AlignBottom);
        chart->addAxis(axisY, Qt::AlignLeft);

        QColor colors[6] = {Qt::red, Qt::green, Qt::blue, Qt::magenta, Qt::cyan, Qt::darkYellow};
        for(int i=0; i<6; i++) {
            series[i] = new QLineSeries();
            series[i]->setName(QString("Spot %1").arg(i+1));
            series[i]->setColor(colors[i]);
            chart->addSeries(series[i]);
            series[i]->attachAxis(axisX);
            series[i]->attachAxis(axisY);
        }

        QChartView* cv = new QChartView(chart);
        cv->setRenderHint(QPainter::Antialiasing);
        l->addWidget(cv);

        QPushButton* back = new QPushButton("Back to Controls");
        back->setStyleSheet("QPushButton { background: #e74c3c; color: white; padding: 10px; } QPushButton:pressed { background: #c0392b; }");
        connect(back, &QPushButton::pressed, [this](){ stack->setCurrentIndex(3); });
        l->addWidget(back);

        stack->addWidget(p); 
    }
};

MainWindow* g_mainWindow = nullptr;

void gui_show_notification(const QString& title, const QString& msg) {
    if(g_mainWindow) {
        QMetaObject::invokeMethod(g_mainWindow, [=](){
            g_mainWindow->lblStatus->setText(title + ": " + msg);
        }, Qt::QueuedConnection);
    }
}

void gui_add_graph_point(int cycle, int spot, int adc_val) {
    if(g_mainWindow) {
        QMetaObject::invokeMethod(g_mainWindow, "appendGraphPoint", Qt::QueuedConnection,
                                  Q_ARG(int, cycle), Q_ARG(int, spot), Q_ARG(int, adc_val));
    }
}

int main(int argc, char *argv[]) {
    curl_global_init(CURL_GLOBAL_DEFAULT);
    
    gpio_handle = lgGpiochipOpen(4); 
    if (gpio_handle < 0) { gpio_handle = lgGpiochipOpen(0); }
    
    lgGpioClaimOutput(gpio_handle, 0, MOTOR1_DIR_PIN, 0);
    lgGpioClaimOutput(gpio_handle, 0, MOTOR1_STEP_PIN, 0);
    lgGpioClaimOutput(gpio_handle, 0, MOTOR1_ENABLE_PIN, 1); 
    lgGpioClaimOutput(gpio_handle, 0, MOTOR2_DIR_PIN, 0);
    lgGpioClaimOutput(gpio_handle, 0, MOTOR2_STEP_PIN, 0);
    lgGpioClaimOutput(gpio_handle, 0, MOTOR2_ENABLE_PIN, 1);
    lgGpioClaimOutput(gpio_handle, 0, LED_PIN, 0);
    lgGpioClaimOutput(gpio_handle, 0, POWER_12V_PIN, 0);
    
    lgGpioClaimInput(gpio_handle, LG_SET_PULL_UP, LIMIT_SWITCH_PIN);
    lgGpioClaimInput(gpio_handle, LG_SET_PULL_UP, LIMIT_SWITCH_2_PIN);

    i2c_handle = lgI2cOpen(I2C_BUS, ADS1115_ADDR, 0);
    
    serial_fd = open(SERIAL_PORT, O_RDWR | O_NOCTTY | O_NDELAY);
    if(serial_fd != -1) {
        struct termios options;
        tcgetattr(serial_fd, &options);
        cfsetispeed(&options, B115200); 
        cfsetospeed(&options, B115200);
        options.c_cflag |= (CLOCAL | CREAD);
        options.c_cflag &= ~PARENB;
        options.c_cflag &= ~CSTOPB;
        options.c_cflag &= ~CSIZE;
        options.c_cflag |= CS8;
        options.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);
        options.c_oflag &= ~OPOST;
        tcsetattr(serial_fd, TCSANOW, &options);
        sleep(2); 
    }

    pthread_t serial_tid;
    pthread_create(&serial_tid, NULL, serial_reader_thread, NULL);

    QApplication app(argc, argv);
    MainWindow w;
    g_mainWindow = &w;
    w.show();
    
    int ret = app.exec();

    if(serial_fd != -1) close(serial_fd);
    lgI2cClose(i2c_handle);
    lgGpiochipClose(gpio_handle);
    curl_global_cleanup();
    return ret;
}

#include "main.moc"
