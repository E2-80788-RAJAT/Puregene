/*
 * puregene_gui.c
 * PureGene Medical Diagnostic Device - Raspberry Pi 5 GUI & Control System in C
 * Display: Raspberry Pi 7-inch Touch Display (800x480 Fullscreen) - White Theme
 * Stepper: Waveshare DRV8825 HAT (Rev 2.1+ Enable=1) on gpiochip0 + Limit Switch Stop
 * Heater & NTC Board: Arduino Due over USB Serial (/dev/ttyACM0)
 * Excel Logging & Email: Generates & Base64-emails PureGene_Temperature_Log.xlsx on Stop or after 1 hour
 */

#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>
#include <cairo.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <termios.h>
#include <time.h>
#include <pthread.h>
#include <curl/curl.h>
#include <xlsxwriter.h>
#include "drv8825.h"

/* ---------------- HARDWARE PIN DEFINITIONS ---------------- */
#define MOTOR2_DIR_PIN      24
#define MOTOR2_STEP_PIN     18
#define MOTOR2_ENABLE_PIN   4

#define LIMIT_SWITCH_PIN    6
#define LIMIT_SWITCH_2_PIN  5

#define MAX_ASSAY_SECONDS   3600
#define TARGET_SETPOINT_C   66.0

/* ---------------- EMAIL CONFIGURATION ---------------- */
#define EXCEL_LOG_FILENAME  "PureGene_Temperature_Log.xlsx"
#define EXCEL_TMP_FILENAME  "PureGene_Temperature_Log.tmp.xlsx"
#define SENDER_EMAIL        "rajatsrivastavaaxon@gmail.com"
#define APP_PASSWORD        "hbsbsbbnwjkpnlzk"

/* ---------------- GLOBAL STATE ---------------- */
static int gpio_handle = -1;
static StepperMotor motor2;
static int serial_fd = -1;

static GtkWidget *stack;
static GtkWidget *lbl_temp1;
static GtkWidget *lbl_temp2;
static GtkWidget *lbl_timer;
static GtkWidget *drawing_area_graph;
static GtkWidget *graph_frame;

static char selected_disease[64] = "HCV";
static volatile int assay_running = 0;
static volatile int board_heaters_confirmed = 0;
static volatile int tray_moving = 0;
static volatile int email_sending = 0;
static int elapsed_seconds = 0;
static gboolean graph_visible = TRUE;

static pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER;

typedef struct {
    int sample_no;
    char timestamp[32];
    int elapsed_s;
    double temp1_top;
    double temp2_bottom;
    int pwm1;
    int pwm2;
} LogEntry;

static LogEntry log_data[MAX_ASSAY_SECONDS + 10];
static int log_count = 0;
static double latest_temp1 = 25.0;
static double latest_temp2 = 25.0;
static int latest_pwm1 = 0;
static int latest_pwm2 = 0;

/* ---------------- EMAIL SENDER FUNCTION (BASE64 MIME ENCODED) ---------------- */
static int send_email_log(const char *filename) {
    CURL *curl;
    CURLcode res = CURLE_OK;
    curl = curl_easy_init();
    if (curl) {
        struct curl_slist *headers = NULL;
        struct curl_slist *recipients = NULL;
        curl_mime *mime;
        curl_mimepart *part;

        curl_easy_setopt(curl, CURLOPT_URL, "smtps://smtp.gmail.com:465");
        curl_easy_setopt(curl, CURLOPT_USERNAME, SENDER_EMAIL);
        curl_easy_setopt(curl, CURLOPT_PASSWORD, APP_PASSWORD);
        curl_easy_setopt(curl, CURLOPT_USE_SSL, CURLUSESSL_ALL);
        curl_easy_setopt(curl, CURLOPT_MAIL_FROM, SENDER_EMAIL);

        recipients = curl_slist_append(recipients, "onlyunacademy@gmail.com");
        recipients = curl_slist_append(recipients, "59sonalisharma@gmail.com");
        curl_easy_setopt(curl, CURLOPT_MAIL_RCPT, recipients);

        headers = curl_slist_append(headers, "From: Raspberry Pi logger <" SENDER_EMAIL ">");
        headers = curl_slist_append(headers, "To: onlyunacademy@gmail.com, 59sonalisharma@gmail.com");
        headers = curl_slist_append(headers, "Subject: Hardware Run Complete: Optical Data Log");
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

        mime = curl_mime_init(curl);

        /* Plain-text email body */
        part = curl_mime_addpart(mime);
        curl_mime_data(part, "Hello,\n\nThe data collection cycle has finished.\n\nBest regards,\nPuregene", CURL_ZERO_TERMINATED);

        /* Binary .xlsx attachment MUST use Base64 transfer encoding for SMTP */
        part = curl_mime_addpart(mime);
        curl_mime_filedata(part, filename);
        curl_mime_filename(part, EXCEL_LOG_FILENAME);
        curl_mime_type(part, "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet");
        curl_mime_encoder(part, "base64");
        curl_mime_name(part, "attachment");

        curl_easy_setopt(curl, CURLOPT_MIMEPOST, mime);

        res = curl_easy_perform(curl);
        if (res != CURLE_OK) {
            fprintf(stderr, "Email send failed: %s\n", curl_easy_strerror(res));
        } else {
            printf("Excel log file (%s) emailed successfully.\n", filename);
        }

        curl_slist_free_all(recipients);
        curl_slist_free_all(headers);
        curl_mime_free(mime);
        curl_easy_cleanup(curl);
    }
    return (int)res;
}

static void *Email_Log_Thread(void *arg) {
    (void)arg;
    send_email_log(EXCEL_LOG_FILENAME);
    email_sending = 0;
    return NULL;
}

static void Trigger_Email_Excel_Async(void) {
    if (email_sending) return;
    email_sending = 1;
    pthread_t tid;
    if (pthread_create(&tid, NULL, Email_Log_Thread, NULL) == 0) {
        pthread_detach(tid);
    } else {
        email_sending = 0;
    }
}

/* ---------------- EXCEL (.XLSX) EXPORT (ATOMIC & XML-SAFE) ---------------- */
static void Write_Excel_Log_File(void) {
    pthread_mutex_lock(&log_mutex);

    lxw_workbook  *workbook  = workbook_new(EXCEL_TMP_FILENAME);
    if (!workbook) {
        pthread_mutex_unlock(&log_mutex);
        fprintf(stderr, "Error: Failed to create temporary Excel workbook.\n");
        return;
    }

    lxw_worksheet *worksheet = workbook_add_worksheet(workbook, "Temperature_Log");

    lxw_format *header_fmt = workbook_add_format(workbook);
    format_set_bold(header_fmt);
    format_set_bg_color(header_fmt, 0x0A2E5C);
    format_set_font_color(header_fmt, LXW_COLOR_WHITE);
    format_set_align(header_fmt, LXW_ALIGN_CENTER);

    lxw_format *num_fmt = workbook_add_format(workbook);
    format_set_num_format(num_fmt, "0.00");

    worksheet_set_column(worksheet, 0, 0, 10, NULL);
    worksheet_set_column(worksheet, 1, 1, 22, NULL);
    worksheet_set_column(worksheet, 2, 3, 18, NULL);
    worksheet_set_column(worksheet, 4, 6, 24, NULL);
    worksheet_set_column(worksheet, 7, 8, 18, NULL);

    worksheet_write_string(worksheet, 0, 0, "Sample #", header_fmt);
    worksheet_write_string(worksheet, 0, 1, "Timestamp", header_fmt);
    worksheet_write_string(worksheet, 0, 2, "Elapsed Time (s)", header_fmt);
    worksheet_write_string(worksheet, 0, 3, "Disease Assay", header_fmt);
    worksheet_write_string(worksheet, 0, 4, "Setpoint (C)", header_fmt);
    worksheet_write_string(worksheet, 0, 5, "Heater Top (C)", header_fmt);
    worksheet_write_string(worksheet, 0, 6, "Heater Bottom (C)", header_fmt);
    worksheet_write_string(worksheet, 0, 7, "Heater Top PWM", header_fmt);
    worksheet_write_string(worksheet, 0, 8, "Heater Bottom PWM", header_fmt);

    for (int i = 0; i < log_count; i++) {
        int row = i + 1;
        double t1 = isfinite(log_data[i].temp1_top) ? log_data[i].temp1_top : 0.0;
        double t2 = isfinite(log_data[i].temp2_bottom) ? log_data[i].temp2_bottom : 0.0;

        worksheet_write_number(worksheet, row, 0, log_data[i].sample_no, NULL);
        worksheet_write_string(worksheet, row, 1, log_data[i].timestamp, NULL);
        worksheet_write_number(worksheet, row, 2, log_data[i].elapsed_s, NULL);
        worksheet_write_string(worksheet, row, 3, selected_disease, NULL);
        worksheet_write_number(worksheet, row, 4, TARGET_SETPOINT_C, num_fmt);
        worksheet_write_number(worksheet, row, 5, t1, num_fmt);
        worksheet_write_number(worksheet, row, 6, t2, num_fmt);
        worksheet_write_number(worksheet, row, 7, log_data[i].pwm1, NULL);
        worksheet_write_number(worksheet, row, 8, log_data[i].pwm2, NULL);
    }

    lxw_error err = workbook_close(workbook);
    if (err == LXW_NO_ERROR) {
        rename(EXCEL_TMP_FILENAME, EXCEL_LOG_FILENAME);
    } else {
        fprintf(stderr, "libxlsxwriter close error: %s\n", lxw_strerror(err));
    }

    pthread_mutex_unlock(&log_mutex);
}

/* ---------------- USB SERIAL COMMUNICATION ---------------- */
static int Init_USB_Serial(void) {
    const char *ports[] = {"/dev/ttyACM0", "/dev/ttyACM1", "/dev/ttyUSB0", "/dev/ttyUSB1"};
    for (int i = 0; i < 4; i++) {
        int fd = open(ports[i], O_RDWR | O_NOCTTY | O_NDELAY);
        if (fd >= 0) {
            struct termios options;
            memset(&options, 0, sizeof(options));
            if (tcgetattr(fd, &options) == 0) {
                cfsetispeed(&options, B115200);
                cfsetospeed(&options, B115200);
                options.c_cflag = (options.c_cflag & ~CSIZE) | CS8;
                options.c_cflag |= (CLOCAL | CREAD);
                options.c_cflag &= ~(PARENB | PARODD | CSTOPB | CRTSCTS);
                options.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);
                options.c_iflag &= ~(IXON | IXOFF | IXANY | IGNBRK | INLCR | ICRNL);
                options.c_oflag = 0;
                tcsetattr(fd, TCSANOW, &options);
                tcflush(fd, TCIOFLUSH);
                printf("Connected to Controller Board on %s\n", ports[i]);
                return fd;
            }
            close(fd);
        }
    }
    return -1;
}

static void Send_Serial_Command(const char *cmd) {
    if (serial_fd < 0) {
        serial_fd = Init_USB_Serial();
    }
    if (serial_fd >= 0) {
        ssize_t res = write(serial_fd, cmd, strlen(cmd));
        if (res < 0 && (errno == EIO || errno == ENXIO || errno == EBADF)) {
            close(serial_fd);
            serial_fd = -1;
        }
    }
}

static void Read_Serial_Telemetry(void) {
    if (serial_fd < 0) {
        serial_fd = Init_USB_Serial();
        if (serial_fd < 0) return;
    }

    static char line_buf[256];
    static int line_pos = 0;
    char read_buf[128];

    int n = read(serial_fd, read_buf, sizeof(read_buf) - 1);
    if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
        close(serial_fd);
        serial_fd = -1;
        return;
    }

    if (n > 0) {
        for (int i = 0; i < n; i++) {
            char ch = read_buf[i];
            if (ch == '\n' || ch == '\r') {
                if (line_pos > 0) {
                    line_buf[line_pos] = '\0';

                    if (strstr(line_buf, "ACK:STARTED") != NULL) {
                        board_heaters_confirmed = 1;
                    } else if (strstr(line_buf, "ACK:STOPPED") != NULL) {
                        board_heaters_confirmed = 0;
                    } else if (strncmp(line_buf, "DATA,", 5) == 0) {
                        double t1 = 0.0, t2 = 0.0;
                        int p1 = 0, p2 = 0, st = 0;
                        if (sscanf(line_buf + 5, "%lf,%lf,%d,%d,%d", &t1, &t2, &p1, &p2, &st) >= 4) {
                            if (isfinite(t1)) latest_temp1 = t1;
                            if (isfinite(t2)) latest_temp2 = t2;
                            latest_pwm1  = p1;
                            latest_pwm2  = p2;
                            board_heaters_confirmed = st;
                        }
                    } else if (strstr(line_buf, "A2:") != NULL) {
                        float t0 = 0.0f, t1 = 0.0f, t2 = 0.0f, t3 = 0.0f;
                        if (sscanf(line_buf, "A0: %fC | A1: %fC | A2: %fC | A3: %fC", &t0, &t1, &t2, &t3) >= 3) {
                            if (isfinite(t2)) latest_temp1 = (double)t2;
                            if (isfinite(t3)) latest_temp2 = (double)t3;
                        }
                    }
                    line_pos = 0;
                }
            } else if (line_pos < (int)(sizeof(line_buf) - 1)) {
                line_buf[line_pos++] = ch;
            }
        }
    }
}

/* ---------------- GPIO & STEPPER MOTOR INITIALIZATION ---------------- */
static void Init_GPIO_And_Stepper(void) {
    gpio_handle = lgGpiochipOpen(0);
    if (gpio_handle < 0) {
        gpio_handle = lgGpiochipOpen(4);
    }
    if (gpio_handle < 0) {
        fprintf(stderr, "Error: Unable to open gpiochip0 or gpiochip4 via lgpio.\n");
        return;
    }

    DRV8825_Init(&motor2, gpio_handle, MOTOR2_DIR_PIN, MOTOR2_STEP_PIN, MOTOR2_ENABLE_PIN);

    lgGpioClaimInput(gpio_handle, LG_SET_PULL_UP, LIMIT_SWITCH_PIN);
    lgGpioClaimInput(gpio_handle, LG_SET_PULL_UP, LIMIT_SWITCH_2_PIN);
}

/* ---------------- STEPPER THREADS (TRAY IN & TRAY OUT) ---------------- */
static void *Tray_In_Thread(void *arg) {
    (void)arg;
    tray_moving = 1;

    if (gpio_handle >= 0) {
        DRV8825_Enable(&motor2, 1);

        int init_sw6 = lgGpioRead(gpio_handle, LIMIT_SWITCH_PIN);
        int init_sw5 = lgGpioRead(gpio_handle, LIMIT_SWITCH_2_PIN);

        if (init_sw6 == 1) {
            if (init_sw5 == 1) {
                while (lgGpioRead(gpio_handle, LIMIT_SWITCH_PIN) == 1 &&
                       lgGpioRead(gpio_handle, LIMIT_SWITCH_2_PIN) == 1) {
                    DRV8825_Turn(&motor2, 1, 0, 600);
                }
            } else {
                while (lgGpioRead(gpio_handle, LIMIT_SWITCH_PIN) == 1) {
                    DRV8825_Turn(&motor2, 1, 0, 600);
                }
            }
        } else if (init_sw5 == 1) {
            while (lgGpioRead(gpio_handle, LIMIT_SWITCH_2_PIN) == 1) {
                DRV8825_Turn(&motor2, 1, 0, 600);
            }
        } else {
            while (lgGpioRead(gpio_handle, LIMIT_SWITCH_PIN) == 0 &&
                   lgGpioRead(gpio_handle, LIMIT_SWITCH_2_PIN) == 0) {
                DRV8825_Turn(&motor2, 1, 0, 600);
            }
        }

        DRV8825_Stop(&motor2);
    }

    tray_moving = 0;
    return NULL;
}

static void *Tray_Out_Thread(void *arg) {
    (void)arg;
    tray_moving = 1;

    if (gpio_handle >= 0) {
        DRV8825_Enable(&motor2, 1);
        DRV8825_Turn(&motor2, 3200, 1, 600);
        DRV8825_Stop(&motor2);
    }

    tray_moving = 0;
    return NULL;
}

/* ---------------- LOGGING & BUTTON CALLBACKS ---------------- */
static void Append_Log_Sample(int elapsed_s) {
    pthread_mutex_lock(&log_mutex);
    if (log_count < MAX_ASSAY_SECONDS) {
        time_t now = time(NULL);
        struct tm *tm_info = localtime(&now);
        LogEntry *entry = &log_data[log_count];
        entry->sample_no = log_count + 1;
        strftime(entry->timestamp, sizeof(entry->timestamp), "%Y-%m-%d %H:%M:%S", tm_info);
        entry->elapsed_s = elapsed_s;
        entry->temp1_top = latest_temp1;
        entry->temp2_bottom = latest_temp2;
        entry->pwm1      = latest_pwm1;
        entry->pwm2      = latest_pwm2;
        log_count++;
    }
    pthread_mutex_unlock(&log_mutex);
}

static void On_Tray_In_Clicked(GtkWidget *widget, gpointer data) {
    (void)widget; (void)data;
    if (tray_moving) return;
    pthread_t tid;
    pthread_create(&tid, NULL, Tray_In_Thread, NULL);
    pthread_detach(tid);
}

static void On_Tray_Out_Clicked(GtkWidget *widget, gpointer data) {
    (void)widget; (void)data;
    if (tray_moving) return;
    pthread_t tid;
    pthread_create(&tid, NULL, Tray_Out_Thread, NULL);
    pthread_detach(tid);
}

static void Stop_Assay_Execution(gboolean send_mail) {
    assay_running = 0;
    board_heaters_confirmed = 0;
    Send_Serial_Command("STOP\n");
    Send_Serial_Command("stop heaters\n");
    Write_Excel_Log_File();
    if (send_mail) {
        Trigger_Email_Excel_Async();
    }
}

static void On_Start_Assay_Clicked(GtkWidget *widget, gpointer data) {
    (void)widget; (void)data;
    if (assay_running) return;

    pthread_mutex_lock(&log_mutex);
    elapsed_seconds = 0;
    log_count = 0;
    pthread_mutex_unlock(&log_mutex);

    /* Record initial t=0 sample so the Excel file always contains data even on short runs */
    Append_Log_Sample(0);

    board_heaters_confirmed = 0;
    assay_running = 1;

    Send_Serial_Command("set_top:66.0\n");
    Send_Serial_Command("set_bot:66.0\n");
    Send_Serial_Command("start heaters\n");
    Send_Serial_Command("START\n");
}

static void On_Stop_Assay_Clicked(GtkWidget *widget, gpointer data) {
    (void)widget; (void)data;
    Stop_Assay_Execution(TRUE);
}

static void On_Graph_Toggle_Clicked(GtkWidget *widget, gpointer data) {
    (void)widget; (void)data;
    graph_visible = !graph_visible;
    if (graph_visible) {
        gtk_widget_show_all(graph_frame);
    } else {
        gtk_widget_hide(graph_frame);
    }
}

static void On_Navigate_Page(GtkWidget *widget, gpointer page_name) {
    (void)widget;
    const char *target = (const char *)page_name;
    if (strcmp(target, "page3") == 0 && assay_running) {
        Stop_Assay_Execution(TRUE);
    }
    gtk_stack_set_visible_child_name(GTK_STACK(stack), target);
}

static void On_Disease_Selected(GtkWidget *widget, gpointer disease_str) {
    (void)widget;
    snprintf(selected_disease, sizeof(selected_disease), "%s", (const char *)disease_str);
    gtk_stack_set_visible_child_name(GTK_STACK(stack), "page4");
}

static gboolean On_Key_Press(GtkWidget *widget, GdkEventKey *event, gpointer data) {
    (void)widget; (void)data;
    if (event->keyval == GDK_KEY_Escape) {
        gtk_main_quit();
        return TRUE;
    }
    return FALSE;
}

/* ---------------- CAIRO REAL-TIME TEMPERATURE GRAPH ---------------- */
static gboolean On_Draw_Graph(GtkWidget *widget, cairo_t *cr, gpointer data) {
    (void)data;
    int width  = gtk_widget_get_allocated_width(widget);
    int height = gtk_widget_get_allocated_height(widget);

    cairo_set_source_rgb(cr, 0.21, 0.25, 0.30);
    cairo_paint(cr);

    int pad_left = 25, pad_right = 20, pad_top = 20, pad_bottom = 25;
    int plot_w = width - pad_left - pad_right;
    int plot_h = height - pad_top - pad_bottom;
    if (plot_w <= 10 || plot_h <= 10) return FALSE;

    double min_temp = 20.0, max_temp = 75.0;

    cairo_set_line_width(cr, 1.0);
    for (int t = 25; t <= 70; t += 15) {
        double y = pad_top + plot_h - ((t - min_temp) / (max_temp - min_temp)) * plot_h;
        cairo_set_source_rgba(cr, 0.45, 0.52, 0.60, 0.35);
        cairo_move_to(cr, pad_left, y);
        cairo_line_to(cr, pad_left + plot_w, y);
        cairo_stroke(cr);
    }

    for (int gx = 1; gx <= 4; gx++) {
        double x = pad_left + ((double)gx / 5.0) * plot_w;
        cairo_set_source_rgba(cr, 0.45, 0.52, 0.60, 0.25);
        cairo_move_to(cr, x, pad_top);
        cairo_line_to(cr, x, pad_top + plot_h);
        cairo_stroke(cr);
    }

    cairo_set_source_rgba(cr, 0.75, 0.80, 0.85, 0.7);
    cairo_move_to(cr, pad_left, pad_top);
    cairo_line_to(cr, pad_left, pad_top + plot_h);
    cairo_line_to(cr, pad_left + plot_w, pad_top + plot_h);
    cairo_stroke(cr);

    double y_set = pad_top + plot_h - ((TARGET_SETPOINT_C - min_temp) / (max_temp - min_temp)) * plot_h;
    double dashes[] = {6.0, 5.0};
    cairo_set_dash(cr, dashes, 2, 0);
    cairo_set_source_rgb(cr, 0.20, 0.85, 0.35);
    cairo_set_line_width(cr, 1.8);
    cairo_move_to(cr, pad_left, y_set);
    cairo_line_to(cr, pad_left + plot_w, y_set);
    cairo_stroke(cr);
    cairo_set_dash(cr, NULL, 0, 0);

    cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
    cairo_set_font_size(cr, 12.0);
    cairo_move_to(cr, pad_left + 6, y_set - 6);
    cairo_show_text(cr, "66°C");

    if (log_count > 1) {
        int max_pts = (log_count > 120) ? 120 : log_count;
        int start_idx = log_count - max_pts;

        cairo_set_source_rgb(cr, 0.0, 0.82, 0.98);
        cairo_set_line_width(cr, 2.5);
        for (int i = 0; i < max_pts; i++) {
            double x = pad_left + ((double)i / (double)(max_pts - 1)) * plot_w;
            double t1 = log_data[start_idx + i].temp1_top;
            if (t1 < min_temp) t1 = min_temp;
            if (t1 > max_temp) t1 = max_temp;
            double y = pad_top + plot_h - ((t1 - min_temp) / (max_temp - min_temp)) * plot_h;
            if (i == 0) cairo_move_to(cr, x, y);
            else cairo_line_to(cr, x, y);
        }
        cairo_stroke(cr);

        cairo_set_source_rgb(cr, 0.95, 0.52, 0.12);
        cairo_set_line_width(cr, 2.5);
        for (int i = 0; i < max_pts; i++) {
            double x = pad_left + ((double)i / (double)(max_pts - 1)) * plot_w;
            double t2 = log_data[start_idx + i].temp2_bottom;
            if (t2 < min_temp) t2 = min_temp;
            if (t2 > max_temp) t2 = max_temp;
            double y = pad_top + plot_h - ((t2 - min_temp) / (max_temp - min_temp)) * plot_h;
            if (i == 0) cairo_move_to(cr, x, y);
            else cairo_line_to(cr, x, y);
        }
        cairo_stroke(cr);
    }

    double leg_x = width - 165;
    double leg_y = height - 48;
    cairo_set_font_size(cr, 12.0);

    cairo_set_source_rgb(cr, 0.0, 0.82, 0.98);
    cairo_move_to(cr, leg_x, leg_y);
    cairo_line_to(cr, leg_x + 18, leg_y);
    cairo_stroke(cr);
    cairo_set_source_rgb(cr, 0.92, 0.95, 0.98);
    cairo_move_to(cr, leg_x + 24, leg_y + 4);
    cairo_show_text(cr, "Heater Top");

    cairo_set_source_rgb(cr, 0.95, 0.52, 0.12);
    cairo_move_to(cr, leg_x, leg_y + 18);
    cairo_line_to(cr, leg_x + 18, leg_y + 18);
    cairo_stroke(cr);
    cairo_set_source_rgb(cr, 0.92, 0.95, 0.98);
    cairo_move_to(cr, leg_x + 24, leg_y + 22);
    cairo_show_text(cr, "Heater Bottom");

    return FALSE;
}

/* ---------------- 1-SECOND PERIODIC TIMER ---------------- */
static gboolean Periodic_Assay_Tick(gpointer data) {
    (void)data;
    Read_Serial_Telemetry();

    if (assay_running && !board_heaters_confirmed) {
        Send_Serial_Command("START\n");
    }

    char t1_buf[64], t2_buf[64];
    snprintf(t1_buf, sizeof(t1_buf), "Heater Top: %.2f °C", latest_temp1);
    snprintf(t2_buf, sizeof(t2_buf), "Heater Bottom: %.2f °C", latest_temp2);
    if (lbl_temp1) gtk_label_set_text(GTK_LABEL(lbl_temp1), t1_buf);
    if (lbl_temp2) gtk_label_set_text(GTK_LABEL(lbl_temp2), t2_buf);

    if (assay_running) {
        elapsed_seconds++;

        int mins = elapsed_seconds / 60;
        int secs = elapsed_seconds % 60;
        char time_buf[64];
        snprintf(time_buf, sizeof(time_buf), "Elapsed: %02d:%02d / 60:00", mins, secs);
        gtk_label_set_text(GTK_LABEL(lbl_timer), time_buf);

        Append_Log_Sample(elapsed_seconds);

        if (elapsed_seconds % 30 == 0 && !email_sending) {
            Write_Excel_Log_File();
        }

        if (drawing_area_graph) {
            gtk_widget_queue_draw(drawing_area_graph);
        }

        /* Auto-stop heaters after 1 hour (3600s), save Excel, and send email */
        if (elapsed_seconds >= MAX_ASSAY_SECONDS) {
            Stop_Assay_Execution(TRUE);
        }
    }
    return G_SOURCE_CONTINUE;
}

static GtkWidget *Load_Scaled_Logo(const char *primary, const char *alt1, const char *alt2, int width, int height) {
    const char *candidates[3] = {primary, alt1, alt2};
    GdkPixbuf *pix = NULL;

    for (int i = 0; i < 3; i++) {
        if (!candidates[i]) continue;
        GError *err = NULL;
        pix = gdk_pixbuf_new_from_file_at_scale(candidates[i], width, height, TRUE, &err);
        if (pix) break;
        if (err) g_error_free(err);
    }

    if (!pix) {
        return gtk_label_new(primary);
    }
    GtkWidget *img = gtk_image_new_from_pixbuf(pix);
    g_object_unref(pix);
    return img;
}

/* ---------------- GUI CONSTRUCTION (WHITE THEME) ---------------- */
static void Apply_Custom_CSS(void) {
    GtkCssProvider *provider = gtk_css_provider_new();
    const char *css =
        "window, stack { background-color: #FFFFFF; color: #0A2540; }"
        ".title-banner { font-size: 54px; font-weight: bold; color: #0A2540; }"
        ".page-heading { font-size: 26px; font-weight: bold; color: #0A2540; }"
        ".tray-btn { font-size: 28px; font-weight: bold; background-image: none; background-color: #0A2E5C; color: #FFFFFF; border-radius: 12px; }"
        ".disease-btn { font-size: 26px; font-weight: bold; background-image: none; background-color: #32383E; color: #FFFFFF; border-radius: 10px; }"
        ".pill-start-btn { font-size: 20px; font-weight: bold; background-image: none; background-color: #389E54; color: #FFFFFF; border-radius: 28px; padding: 10px 28px; }"
        ".back-btn { font-size: 18px; font-weight: bold; background-image: none; background-color: #C92A2A; color: #FFFFFF; border-radius: 10px; padding: 10px 20px; }"
        ".next-btn { font-size: 18px; font-weight: bold; background-image: none; background-color: #389E54; color: #FFFFFF; border-radius: 10px; padding: 10px 22px; }"
        ".start-ctrl-btn { font-size: 20px; font-weight: bold; background-image: none; background-color: #389E54; color: #FFFFFF; border-radius: 10px; }"
        ".stop-ctrl-btn { font-size: 20px; font-weight: bold; background-image: none; background-color: #C92A2A; color: #FFFFFF; border-radius: 10px; }"
        ".graph-ctrl-btn { font-size: 20px; font-weight: bold; background-image: none; background-color: #E5A919; color: #111111; border-radius: 10px; }"
        ".telemetry-bar { background-color: #102A45; padding: 10px 16px; }"
        ".temp-readout { font-size: 16px; font-weight: bold; color: #FFFFFF; }";
    gtk_css_provider_load_from_data(provider, css, -1, NULL);
    gtk_style_context_add_provider_for_screen(gdk_screen_get_default(),
                                              GTK_STYLE_PROVIDER(provider),
                                              GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(provider);
}

static GtkWidget *Build_Page1_Home(void) {
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 15);
    gtk_container_set_border_width(GTK_CONTAINER(vbox), 24);

    GtkWidget *logo_bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    GtkWidget *img_aki  = Load_Scaled_Logo("aki.jpeg", "aki.png", "aki.avif", 210, 90);
    GtkWidget *img_iit  = Load_Scaled_Logo("iit.png", "iit.jpeg", NULL, 115, 115);
    GtkWidget *img_icmr = Load_Scaled_Logo("icmr.png", "icmr.jpeg", NULL, 180, 95);

    gtk_box_pack_start(GTK_BOX(logo_bar), img_aki, FALSE, FALSE, 0);
    gtk_box_set_center_widget(GTK_BOX(logo_bar), img_iit);
    gtk_box_pack_end(GTK_BOX(logo_bar), img_icmr, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), logo_bar, FALSE, FALSE, 5);

    GtkWidget *lbl_name = gtk_label_new("PureGene");
    gtk_widget_set_halign(lbl_name, GTK_ALIGN_CENTER);
    gtk_style_context_add_class(gtk_widget_get_style_context(lbl_name), "title-banner");
    gtk_box_pack_start(GTK_BOX(vbox), lbl_name, TRUE, TRUE, 10);

    GtkWidget *btn_enter = gtk_button_new_with_label("Touch to Start  ➔");
    gtk_style_context_add_class(gtk_widget_get_style_context(btn_enter), "pill-start-btn");
    gtk_widget_set_size_request(btn_enter, 260, 56);
    gtk_widget_set_halign(btn_enter, GTK_ALIGN_CENTER);
    g_signal_connect(btn_enter, "clicked", G_CALLBACK(On_Navigate_Page), "page2");
    gtk_box_pack_end(GTK_BOX(vbox), btn_enter, FALSE, FALSE, 15);

    return vbox;
}

static GtkWidget *Build_Page2_TrayControl(void) {
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 15);
    gtk_container_set_border_width(GTK_CONTAINER(vbox), 24);

    GtkWidget *lbl_hdr = gtk_label_new("PureGene — Cartridge Tray Control");
    gtk_style_context_add_class(gtk_widget_get_style_context(lbl_hdr), "page-heading");
    gtk_box_pack_start(GTK_BOX(vbox), lbl_hdr, FALSE, FALSE, 5);

    GtkWidget *hbox_rects = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 35);
    gtk_widget_set_halign(hbox_rects, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(hbox_rects, GTK_ALIGN_CENTER);

    GtkWidget *btn_tray_in  = gtk_button_new_with_label("Tray In");
    GtkWidget *btn_tray_out = gtk_button_new_with_label("Tray Out");
    gtk_widget_set_size_request(btn_tray_in, 310, 185);
    gtk_widget_set_size_request(btn_tray_out, 310, 185);
    gtk_style_context_add_class(gtk_widget_get_style_context(btn_tray_in), "tray-btn");
    gtk_style_context_add_class(gtk_widget_get_style_context(btn_tray_out), "tray-btn");

    g_signal_connect(btn_tray_in, "clicked", G_CALLBACK(On_Tray_In_Clicked), NULL);
    g_signal_connect(btn_tray_out, "clicked", G_CALLBACK(On_Tray_Out_Clicked), NULL);

    gtk_box_pack_start(GTK_BOX(hbox_rects), btn_tray_in, FALSE, FALSE, 5);
    gtk_box_pack_start(GTK_BOX(hbox_rects), btn_tray_out, FALSE, FALSE, 5);
    gtk_box_pack_start(GTK_BOX(vbox), hbox_rects, TRUE, TRUE, 10);

    GtkWidget *nav_bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    GtkWidget *btn_back = gtk_button_new_with_label("❮ Back");
    gtk_widget_set_size_request(btn_back, 140, 52);
    gtk_style_context_add_class(gtk_widget_get_style_context(btn_back), "back-btn");
    g_signal_connect(btn_back, "clicked", G_CALLBACK(On_Navigate_Page), "page1");

    GtkWidget *btn_next = gtk_button_new_with_label("Select Disease  ➔");
    gtk_widget_set_size_request(btn_next, 230, 52);
    gtk_style_context_add_class(gtk_widget_get_style_context(btn_next), "next-btn");
    g_signal_connect(btn_next, "clicked", G_CALLBACK(On_Navigate_Page), "page3");

    gtk_box_pack_start(GTK_BOX(nav_bar), btn_back, FALSE, FALSE, 0);
    gtk_box_pack_end(GTK_BOX(nav_bar), btn_next, FALSE, FALSE, 0);
    gtk_box_pack_end(GTK_BOX(vbox), nav_bar, FALSE, FALSE, 0);

    return vbox;
}

static GtkWidget *Build_Page3_DiseaseSelect(void) {
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_container_set_border_width(GTK_CONTAINER(vbox), 24);

    GtkWidget *lbl_select = gtk_label_new("Select Disease");
    gtk_style_context_add_class(gtk_widget_get_style_context(lbl_select), "page-heading");
    gtk_box_pack_start(GTK_BOX(vbox), lbl_select, FALSE, FALSE, 4);

    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 22);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 30);
    gtk_widget_set_halign(grid, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(grid, GTK_ALIGN_CENTER);

    const char *diseases[4] = {"HCV", "HPV", "TB", "Carbapenem"};
    for (int i = 0; i < 4; i++) {
        GtkWidget *btn = gtk_button_new_with_label(diseases[i]);
        gtk_widget_set_size_request(btn, 320, 110);
        gtk_style_context_add_class(gtk_widget_get_style_context(btn), "disease-btn");
        g_signal_connect(btn, "clicked", G_CALLBACK(On_Disease_Selected), (gpointer)diseases[i]);
        gtk_grid_attach(GTK_GRID(grid), btn, i % 2, i / 2, 1, 1);
    }
    gtk_box_pack_start(GTK_BOX(vbox), grid, TRUE, TRUE, 5);

    GtkWidget *bottom_bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    GtkWidget *btn_back = gtk_button_new_with_label("❮ Back");
    gtk_widget_set_size_request(btn_back, 140, 52);
    gtk_style_context_add_class(gtk_widget_get_style_context(btn_back), "back-btn");
    g_signal_connect(btn_back, "clicked", G_CALLBACK(On_Navigate_Page), "page2");

    gtk_box_pack_start(GTK_BOX(bottom_bar), btn_back, FALSE, FALSE, 0);
    gtk_box_pack_end(GTK_BOX(vbox), bottom_bar, FALSE, FALSE, 0);

    return vbox;
}

static GtkWidget *Build_Page4_AssayExecution(void) {
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

    GtkWidget *info_bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 20);
    gtk_style_context_add_class(gtk_widget_get_style_context(info_bar), "telemetry-bar");

    lbl_temp1 = gtk_label_new("Heater Top: 25.00 °C");
    lbl_temp2 = gtk_label_new("Heater Bottom: 25.00 °C");
    lbl_timer = gtk_label_new("Elapsed: 00:00 / 60:00");
    gtk_style_context_add_class(gtk_widget_get_style_context(lbl_temp1), "temp-readout");
    gtk_style_context_add_class(gtk_widget_get_style_context(lbl_temp2), "temp-readout");
    gtk_style_context_add_class(gtk_widget_get_style_context(lbl_timer), "temp-readout");

    gtk_box_pack_start(GTK_BOX(info_bar), lbl_temp1, TRUE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(info_bar), lbl_temp2, TRUE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(info_bar), lbl_timer, TRUE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), info_bar, FALSE, FALSE, 0);

    graph_frame = gtk_frame_new(NULL);
    gtk_frame_set_shadow_type(GTK_FRAME(graph_frame), GTK_SHADOW_NONE);
    drawing_area_graph = gtk_drawing_area_new();
    gtk_widget_set_size_request(drawing_area_graph, 780, 330);
    g_signal_connect(G_OBJECT(drawing_area_graph), "draw", G_CALLBACK(On_Draw_Graph), NULL);
    gtk_container_add(GTK_CONTAINER(graph_frame), drawing_area_graph);
    gtk_box_pack_start(GTK_BOX(vbox), graph_frame, TRUE, TRUE, 0);

    GtkWidget *ctrl_bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 16);
    gtk_container_set_border_width(GTK_CONTAINER(ctrl_bar), 12);

    GtkWidget *btn_back = gtk_button_new_with_label("◀ Back");
    gtk_widget_set_size_request(btn_back, 165, 58);
    gtk_style_context_add_class(gtk_widget_get_style_context(btn_back), "back-btn");
    g_signal_connect(btn_back, "clicked", G_CALLBACK(On_Navigate_Page), "page3");

    GtkWidget *btn_start = gtk_button_new_with_label("▶ Start");
    gtk_widget_set_size_request(btn_start, 180, 58);
    gtk_style_context_add_class(gtk_widget_get_style_context(btn_start), "start-ctrl-btn");
    g_signal_connect(btn_start, "clicked", G_CALLBACK(On_Start_Assay_Clicked), NULL);

    GtkWidget *btn_stop = gtk_button_new_with_label("■ Stop");
    gtk_widget_set_size_request(btn_stop, 180, 58);
    gtk_style_context_add_class(gtk_widget_get_style_context(btn_stop), "stop-ctrl-btn");
    g_signal_connect(btn_stop, "clicked", G_CALLBACK(On_Stop_Assay_Clicked), NULL);

    GtkWidget *btn_graph = gtk_button_new_with_label("📶 Graph");
    gtk_widget_set_size_request(btn_graph, 180, 58);
    gtk_style_context_add_class(gtk_widget_get_style_context(btn_graph), "graph-ctrl-btn");
    g_signal_connect(btn_graph, "clicked", G_CALLBACK(On_Graph_Toggle_Clicked), NULL);

    gtk_box_pack_start(GTK_BOX(ctrl_bar), btn_back, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(ctrl_bar), btn_start, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(ctrl_bar), btn_stop, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(ctrl_bar), btn_graph, TRUE, TRUE, 0);
    gtk_box_pack_end(GTK_BOX(vbox), ctrl_bar, FALSE, FALSE, 0);

    return vbox;
}

int main(int argc, char *argv[]) {
    curl_global_init(CURL_GLOBAL_DEFAULT);
    gtk_init(&argc, &argv);
    Apply_Custom_CSS();

    Init_GPIO_And_Stepper();
    serial_fd = Init_USB_Serial();

    GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), "PureGene Medical Diagnostic System");
    gtk_window_set_default_size(GTK_WINDOW(window), 800, 480);
    gtk_window_set_decorated(GTK_WINDOW(window), FALSE);
    gtk_window_fullscreen(GTK_WINDOW(window));

    g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), NULL);
    g_signal_connect(window, "key-press-event", G_CALLBACK(On_Key_Press), NULL);

    stack = gtk_stack_new();
    gtk_stack_set_transition_type(GTK_STACK(stack), GTK_STACK_TRANSITION_TYPE_SLIDE_LEFT_RIGHT);

    gtk_stack_add_named(GTK_STACK(stack), Build_Page1_Home(), "page1");
    gtk_stack_add_named(GTK_STACK(stack), Build_Page2_TrayControl(), "page2");
    gtk_stack_add_named(GTK_STACK(stack), Build_Page3_DiseaseSelect(), "page3");
    gtk_stack_add_named(GTK_STACK(stack), Build_Page4_AssayExecution(), "page4");

    gtk_container_add(GTK_CONTAINER(window), stack);
    gtk_widget_show_all(window);

    g_timeout_add(1000, Periodic_Assay_Tick, NULL);

    gtk_main();

    Send_Serial_Command("STOP\n");
    Send_Serial_Command("stop heaters\n");
    if (serial_fd >= 0) close(serial_fd);
    if (gpio_handle >= 0) {
        DRV8825_Stop(&motor2);
        lgGpiochipClose(gpio_handle);
    }

    int wait_ticks = 0;
    while (email_sending && wait_ticks < 50) {
        usleep(100000);
        wait_ticks++;
    }
    curl_global_cleanup();
    return 0;
}
