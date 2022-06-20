#include <application.h>

#define SEND_DATA_INTERVAL (15 * 60 * 1000)

#define BATTERY_UPDATE_INTERVAL (60 * 60 * 1000)
#define TMP112_UPDATE_INTERVAL (2 * 60 * 1000)
#define HUMIDITY_TAG_UPDATE_INTERVAL (2 * 60 * 1000)
#define BAROMETER_TAG_UPDATE_INTERVAL (2 * 60 * 1000)
#define VOC_LP_TAG_UPDATE_INTERVAL (5 * 60 * 1000)
#define CO2_UPDATE_INTERVAL (5 * 60 * 1000)

#define MAX_PAGE_INDEX 3
#define PAGE_INDEX_MENU -1

#define CALIBRATION_START_DELAY (5 * 60 * 1000)
#define CALIBRATION_MEASURE_INTERVAL (1 * 60 * 1000)
#define CALIBRATION_COUNTER 15

#define LORA_START_DELAY (10 * 1000)

/*
#define CALIBRATION_START_DELAY (5 * 1000)
#define CALIBRATION_MEASURE_INTERVAL (5 * 1000)
#define CALIBRATION_COUNTER 10
*/

enum {
    HEADER_BOOT         = 0x00,
    HEADER_UPDATE       = 0x01,
    HEADER_BUTTON_CLICK = 0x02,
    HEADER_BUTTON_HOLD  = 0x03,

} header = HEADER_BOOT;

twr_led_t led;
bool led_state = false;

// Lora instance
twr_cmwx1zzabz_t lora;

twr_led_t lcd_led_r;
twr_led_t lcd_led_g;
twr_led_t lcd_led_b;

static struct
{
    float_t temperature;
    float_t humidity;
    float_t tvoc;
    float_t pressure;
    float_t altitude;
    float_t co2_concentation;
    float_t battery_voltage;
    float_t battery_pct;

} values;

static const struct
{
    char *name0;
    char *format0;
    float_t *value0;
    char *unit0;
    char *name1;
    char *format1;
    float_t *value1;
    char *unit1;

} pages[] = {
    {"Temperature   ", "%.1f", &values.temperature, "\xb0" "C",
     "Humidity      ", "%.1f", &values.humidity, "%"},
    {"CO2           ", "%.0f", &values.co2_concentation, "ppm",
     "TVOC          ", "%.1f", &values.tvoc, "ppb"},
    {"Pressure      ", "%.0f", &values.pressure, "hPa",
     "Altitude      ", "%.1f", &values.altitude, "m"},
    {"Battery       ", "%.2f", &values.battery_voltage, "V",
     "Battery       ", "%.0f", &values.battery_pct, "%"},
};

static int page_index = 0;
static int menu_item = 0;

static struct
{
    twr_tick_t next_update;
    bool mqtt;

} lcd;

void battery_event_handler(twr_module_battery_event_t event, void *event_param);

static void lcd_page_render();
static void humidity_tag_init(twr_tag_humidity_revision_t revision, twr_i2c_channel_t i2c_channel, humidity_tag_t *tag);
static void barometer_tag_init(twr_i2c_channel_t i2c_channel, barometer_tag_t *tag);

void lcd_event_handler(twr_module_lcd_event_t event, void *event_param);
void tmp112_event_handler(twr_tmp112_t *self, twr_tmp112_event_t event, void *event_param);
void humidity_tag_event_handler(twr_tag_humidity_t *self, twr_tag_humidity_event_t event, void *event_param);
void barometer_tag_event_handler(twr_tag_barometer_t *self, twr_tag_barometer_event_t event, void *event_param);
void co2_event_handler(twr_module_co2_event_t event, void *event_param);
void voc_lp_tag_event_handler(twr_tag_voc_lp_t *self, twr_tag_voc_lp_event_t event, void *event_param);

bool at_send(void);
bool at_status(void);
bool at_calibration(void);

void calibration_task(void *param);
void lora_task(void *param);

twr_scheduler_task_id_t calibration_task_id = 0;
twr_scheduler_task_id_t lora_task_id = 0;

int calibration_counter;

static void lcd_page_render()
{
    int w;
    char str[32];

    twr_system_pll_enable();

    twr_module_lcd_clear();

    if ((page_index <= MAX_PAGE_INDEX) && (page_index != PAGE_INDEX_MENU))
    {
        twr_module_lcd_set_font(&twr_font_ubuntu_15);
        twr_module_lcd_draw_string(10, 5, pages[page_index].name0, true);

        twr_module_lcd_set_font(&twr_font_ubuntu_28);
        snprintf(str, sizeof(str), pages[page_index].format0, *pages[page_index].value0);
        w = twr_module_lcd_draw_string(25, 25, str, true);
        twr_module_lcd_set_font(&twr_font_ubuntu_15);
        w = twr_module_lcd_draw_string(w, 35, pages[page_index].unit0, true);

        twr_module_lcd_set_font(&twr_font_ubuntu_15);
        twr_module_lcd_draw_string(10, 55, pages[page_index].name1, true);

        twr_module_lcd_set_font(&twr_font_ubuntu_28);
        snprintf(str, sizeof(str), pages[page_index].format1, *pages[page_index].value1);
        w = twr_module_lcd_draw_string(25, 75, str, true);
        twr_module_lcd_set_font(&twr_font_ubuntu_15);
        twr_module_lcd_draw_string(w, 85, pages[page_index].unit1, true);
    }

    snprintf(str, sizeof(str), "%d/%d", page_index + 1, MAX_PAGE_INDEX + 1);
    twr_module_lcd_set_font(&twr_font_ubuntu_13);
    twr_module_lcd_draw_string(55, 115, str, true);

    twr_system_pll_disable();
}

static void humidity_tag_init(twr_tag_humidity_revision_t revision, twr_i2c_channel_t i2c_channel, humidity_tag_t *tag)
{
    memset(tag, 0, sizeof(*tag));

    if (revision == TWR_TAG_HUMIDITY_REVISION_R1)
    {
        tag->param.channel = TWR_RADIO_PUB_CHANNEL_R1_I2C0_ADDRESS_DEFAULT;
    }
    else if (revision == TWR_TAG_HUMIDITY_REVISION_R2)
    {
        tag->param.channel = TWR_RADIO_PUB_CHANNEL_R2_I2C0_ADDRESS_DEFAULT;
    }
    else if (revision == TWR_TAG_HUMIDITY_REVISION_R3)
    {
        tag->param.channel = TWR_RADIO_PUB_CHANNEL_R3_I2C0_ADDRESS_DEFAULT;
    }
    else
    {
        return;
    }

    if (i2c_channel == TWR_I2C_I2C1)
    {
        tag->param.channel |= 0x80;
    }

    twr_tag_humidity_init(&tag->self, revision, i2c_channel, TWR_TAG_HUMIDITY_I2C_ADDRESS_DEFAULT);

    twr_tag_humidity_set_update_interval(&tag->self, HUMIDITY_TAG_UPDATE_INTERVAL);

    twr_tag_humidity_set_event_handler(&tag->self, humidity_tag_event_handler, &tag->param);
}

static void barometer_tag_init(twr_i2c_channel_t i2c_channel, barometer_tag_t *tag)
{
    memset(tag, 0, sizeof(*tag));

    tag->param.channel = TWR_RADIO_PUB_CHANNEL_R1_I2C0_ADDRESS_DEFAULT;

    twr_tag_barometer_init(&tag->self, i2c_channel);

    twr_tag_barometer_set_update_interval(&tag->self, BAROMETER_TAG_UPDATE_INTERVAL);

    twr_tag_barometer_set_event_handler(&tag->self, barometer_tag_event_handler, &tag->param);
}

void lcd_event_handler(twr_module_lcd_event_t event, void *event_param)
{
    (void) event_param;

    if (event == TWR_MODULE_LCD_EVENT_LEFT_CLICK)
    {
        if ((page_index != PAGE_INDEX_MENU))
        {
            // Key previous page
            page_index--;
            if (page_index < 0)
            {
                page_index = MAX_PAGE_INDEX;
                menu_item = 0;
            }
        }
        else
        {
            // Key menu down
            menu_item++;
            if (menu_item > 4)
            {
                menu_item = 0;
            }
        }

        static uint16_t left_event_count = 0;
        left_event_count++;
        //twr_radio_pub_event_count(TWR_RADIO_PUB_EVENT_LCD_BUTTON_LEFT, &left_event_count);
    }
    else if(event == TWR_MODULE_LCD_EVENT_RIGHT_CLICK)
    {
        if ((page_index != PAGE_INDEX_MENU) || (menu_item == 0))
        {
            // Key next page
            page_index++;
            if (page_index > MAX_PAGE_INDEX)
            {
                page_index = 0;
            }
            if (page_index == PAGE_INDEX_MENU)
            {
                menu_item = 0;
            }
        }

        static uint16_t right_event_count = 0;
        right_event_count++;
        //twr_radio_pub_event_count(TWR_RADIO_PUB_EVENT_LCD_BUTTON_RIGHT, &right_event_count);
    }
    else if(event == TWR_MODULE_LCD_EVENT_LEFT_HOLD)
    {
        twr_cmwx1zzabz_join(&lora);

        twr_led_pulse(&lcd_led_g, 1000);
    }
    else if(event == TWR_MODULE_LCD_EVENT_RIGHT_HOLD)
    {
        at_calibration();

        twr_led_pulse(&lcd_led_g, 1000);

    }
    else if(event == TWR_MODULE_LCD_EVENT_BOTH_HOLD)
    {
        header = HEADER_BUTTON_HOLD;
        twr_scheduler_plan_now(lora_task_id);

        twr_led_pulse(&lcd_led_g, 1000);
    }

    twr_scheduler_plan_now(0);
}


void voc_lp_tag_event_handler(twr_tag_voc_lp_t *self, twr_tag_voc_lp_event_t event, void *event_param)
{
    if (event == TWR_TAG_VOC_LP_EVENT_UPDATE)
    {
        uint16_t value;

        if (twr_tag_voc_lp_get_tvoc_ppb(self, &value))
        {
                values.tvoc = value;
                twr_scheduler_plan_now(0);
        }
    }
}

void tmp112_event_handler(twr_tmp112_t *self, twr_tmp112_event_t event, void *event_param)
{
    float value;

    if (event != TWR_TMP112_EVENT_UPDATE)
    {
        return;
    }

    if (twr_tmp112_get_temperature_celsius(self, &value))
    {
            values.temperature = value;
            twr_scheduler_plan_now(0);
    }
}

void humidity_tag_event_handler(twr_tag_humidity_t *self, twr_tag_humidity_event_t event, void *event_param)
{
    float value;

    if (event != TWR_TAG_HUMIDITY_EVENT_UPDATE)
    {
        return;
    }

    if (twr_tag_humidity_get_humidity_percentage(self, &value))
    {
            values.humidity = value;
            twr_scheduler_plan_now(0);
    }
}

void barometer_tag_event_handler(twr_tag_barometer_t *self, twr_tag_barometer_event_t event, void *event_param)
{
    float pascal;
    float meter;

    if (event != TWR_TAG_BAROMETER_EVENT_UPDATE)
    {
        return;
    }

    if (!twr_tag_barometer_get_pressure_pascal(self, &pascal))
    {
        return;
    }


    if (!twr_tag_barometer_get_altitude_meter(self, &meter))
    {
        return;
    }

    values.pressure = pascal / 100.0;
    values.altitude = meter;
    twr_scheduler_plan_now(0);
}

void co2_event_handler(twr_module_co2_event_t event, void *event_param)
{
    float value;

    if (event == TWR_MODULE_CO2_EVENT_UPDATE)
    {
        if (twr_module_co2_get_concentration_ppm(&value))
        {
                values.co2_concentation = value;
                twr_scheduler_plan_now(0);
        }
    }
}

void battery_event_handler(twr_module_battery_event_t event, void *event_param)
{
    (void) event;
    (void) event_param;

    float voltage;
    int percentage;

    if(event == TWR_MODULE_BATTERY_EVENT_UPDATE)
    {
        if (twr_module_battery_get_voltage(&voltage))
        {

            values.battery_voltage = voltage;
            //twr_radio_pub_battery(&voltage);
        }

        if (twr_module_battery_get_charge_level(&percentage))
        {
            values.battery_pct = percentage;
        }
    }
}


void calibration_start()
{
    calibration_counter = CALIBRATION_COUNTER;

    twr_led_set_mode(&led, TWR_LED_MODE_BLINK_FAST);
    twr_led_set_mode(&lcd_led_b, TWR_LED_MODE_BLINK_FAST);
    calibration_task_id = twr_scheduler_register(calibration_task, NULL, twr_tick_get() + CALIBRATION_START_DELAY);
    twr_atci_printfln("$CO2_CALIBRATION: \"START\"");

}

void calibration_stop()
{
    if (!calibration_task_id)
    {
        return;
    }

    twr_led_set_mode(&led, TWR_LED_MODE_OFF);
    twr_led_set_mode(&lcd_led_b, TWR_LED_MODE_OFF);
    twr_scheduler_unregister(calibration_task_id);
    calibration_task_id = 0;

    twr_module_co2_set_update_interval(CO2_UPDATE_INTERVAL);
    twr_atci_printfln("$CO2_CALIBRATION: \"STOP\"");
}

void calibration_task(void *param)
{
    (void) param;

    twr_led_set_mode(&led, TWR_LED_MODE_BLINK_SLOW);
    twr_led_set_mode(&lcd_led_b, TWR_LED_MODE_BLINK_SLOW);

    twr_atci_printfln("$CO2_CALIBRATION_COUNTER: \"%d\"", calibration_counter);

    twr_module_co2_set_update_interval(CALIBRATION_MEASURE_INTERVAL);
    twr_module_co2_calibration(TWR_LP8_CALIBRATION_BACKGROUND_FILTERED);

    calibration_counter--;

    if (calibration_counter == 0)
    {
        calibration_stop();
        return;
    }

    twr_scheduler_plan_current_relative(CALIBRATION_MEASURE_INTERVAL);
}

bool at_calibration(void)
{
    if (calibration_task_id)
    {
        calibration_stop();
    }
    else
    {
        calibration_start();
    }

    return true;
}


bool at_status(void)
{
    static const struct {
        float *value;
        const char *name;
        int precision;
    } items[] = {
            {&values.battery_voltage, "Voltage", 1},
            {&values.temperature, "Temperature", 1},
            {&values.temperature, "Humidity", 1},
            {&values.tvoc, "TVOC", 0},
            {&values.pressure, "Pressure", 0},
            {&values.co2_concentation, "CO2", 0},
    };

    for (size_t i = 0; i < sizeof(items) / sizeof(items[0]); i++)
    {
        if (isnan(*items[i].value))
        {
            twr_atci_printfln("$STATUS: \"%s\",", items[i].name);
        }
        else
        {
            twr_atci_printfln("$STATUS: \"%s\",%.*f", items[i].name, items[i].precision, *items[i].value);
        }
    }

    return true;
}


bool at_send(void)
{
    twr_scheduler_plan_now(lora_task_id);

    return true;
}

void lora_callback(twr_cmwx1zzabz_t *self, twr_cmwx1zzabz_event_t event, void *event_param)
{
    twr_scheduler_plan_now(0);

    if (event == TWR_CMWX1ZZABZ_EVENT_ERROR)
    {
        twr_log_debug("$LORA_ERROR");
        twr_led_set_mode(&led, TWR_LED_MODE_BLINK_SLOW);
        twr_led_set_mode(&lcd_led_r, TWR_LED_MODE_BLINK_SLOW);
    }
    else if (event == TWR_CMWX1ZZABZ_EVENT_SEND_MESSAGE_START)
    {
        twr_led_pulse(&led, 500);
    }
    else if (event == TWR_CMWX1ZZABZ_EVENT_SEND_MESSAGE_DONE)
    {
        //strcpy(str_status, "SENT...");
    }
    else if (event == TWR_CMWX1ZZABZ_EVENT_MESSAGE_CONFIRMED)
    {
        twr_led_pulse(&lcd_led_g, 500);
    }
    else if (event == TWR_CMWX1ZZABZ_EVENT_MESSAGE_NOT_CONFIRMED)
    {
        twr_led_pulse(&lcd_led_r, 2000);
    }
    else if (event == TWR_CMWX1ZZABZ_EVENT_MESSAGE_RETRANSMISSION)
    {
        twr_led_pulse(&lcd_led_g, 500);
    }
    else if (event == TWR_CMWX1ZZABZ_EVENT_READY)
    {
        twr_led_set_mode(&led, TWR_LED_MODE_OFF);
        twr_led_set_mode(&lcd_led_r, TWR_LED_MODE_OFF);
    }
    else if (event == TWR_CMWX1ZZABZ_EVENT_JOIN_SUCCESS)
    {
        twr_atci_printfln("$JOIN_OK");
        twr_led_pulse(&lcd_led_g, 2000);
    }
    else if (event == TWR_CMWX1ZZABZ_EVENT_JOIN_ERROR)
    {
        twr_atci_printfln("$JOIN_ERROR");
        twr_led_pulse(&lcd_led_r, 2000);
    }
}


void application_init(void)
{
    twr_log_init(TWR_LOG_LEVEL_DUMP, TWR_LOG_TIMESTAMP_ABS);

    twr_led_init(&led, TWR_GPIO_LED, false, false);
    twr_led_set_mode(&led, TWR_LED_MODE_OFF);

    twr_radio_init(TWR_RADIO_MODE_NODE_SLEEPING);

    // Temperature
    static twr_tmp112_t temperature;
    twr_tmp112_init(&temperature, TWR_I2C_I2C0, 0x49);
    twr_tmp112_set_event_handler(&temperature, tmp112_event_handler, NULL);
    twr_tmp112_set_update_interval(&temperature, TMP112_UPDATE_INTERVAL);

    // Hudmidity
    static humidity_tag_t humidity_tag_0_0;
    humidity_tag_init(TWR_TAG_HUMIDITY_REVISION_R1, TWR_I2C_I2C0, &humidity_tag_0_0);

    static humidity_tag_t humidity_tag_0_2;
    humidity_tag_init(TWR_TAG_HUMIDITY_REVISION_R2, TWR_I2C_I2C0, &humidity_tag_0_2);

    static humidity_tag_t humidity_tag_0_4;
    humidity_tag_init(TWR_TAG_HUMIDITY_REVISION_R3, TWR_I2C_I2C0, &humidity_tag_0_4);

    // Barometer
    static barometer_tag_t barometer_tag_0_0;
    barometer_tag_init(TWR_I2C_I2C0, &barometer_tag_0_0);

    // CO2
    twr_module_co2_init();
    twr_module_co2_set_update_interval(CO2_UPDATE_INTERVAL);
    twr_module_co2_set_event_handler(co2_event_handler, NULL);

    // VOC-LP
    static twr_tag_voc_lp_t voc_lp;
    twr_tag_voc_lp_init(&voc_lp, TWR_I2C_I2C0);
    twr_tag_voc_lp_set_event_handler(&voc_lp, voc_lp_tag_event_handler, NULL);
    twr_tag_voc_lp_set_update_interval(&voc_lp, VOC_LP_TAG_UPDATE_INTERVAL);

    // LCD
    memset(&values, 0xff, sizeof(values));
    twr_module_lcd_init();
    twr_module_lcd_set_event_handler(lcd_event_handler, NULL);
    twr_module_lcd_set_button_hold_time(5000);
    const twr_led_driver_t *led_driver = twr_module_lcd_get_led_driver();
    twr_led_init_virtual(&lcd_led_r, TWR_MODULE_LCD_LED_RED, led_driver, 1);
    twr_led_init_virtual(&lcd_led_g, TWR_MODULE_LCD_LED_GREEN, led_driver, 1);
    twr_led_init_virtual(&lcd_led_b, TWR_MODULE_LCD_LED_BLUE, led_driver, 1);

    // Battery
    twr_module_battery_init();
    twr_module_battery_set_event_handler(battery_event_handler, NULL);
    twr_module_battery_set_update_interval(BATTERY_UPDATE_INTERVAL);

    // Initialize lora module
    twr_cmwx1zzabz_init(&lora, TWR_UART_UART1);
    twr_cmwx1zzabz_set_event_handler(&lora, lora_callback, NULL);
    twr_cmwx1zzabz_set_class(&lora, TWR_CMWX1ZZABZ_CONFIG_CLASS_A);
    twr_cmwx1zzabz_set_debug(&lora, true);

    // Initialize AT command interface
    twr_at_lora_init(&lora);
    static const twr_atci_command_t commands[] = {
            TWR_AT_LORA_COMMANDS,
            {"$SEND", at_send, NULL, NULL, NULL, "Immediately send packet"},
            {"$CALIBRATION", at_calibration, NULL, NULL, NULL, "Calibrate CO2 in outdoor to 400ppm"},
            {"$STATUS", at_status, NULL, NULL, NULL, "Show status"},
            TWR_ATCI_COMMAND_CLAC,
            TWR_ATCI_COMMAND_HELP
    };
    twr_atci_init(commands, TWR_ATCI_COMMANDS_LENGTH(commands));

    lora_task_id = twr_scheduler_register(lora_task, NULL, twr_tick_get() + LORA_START_DELAY);

    twr_led_pulse(&led, 2000);
}

void application_task(void)
{
    if (!twr_module_lcd_is_ready())
    {
        return;
    }

    if (!lcd.mqtt)
    {
        lcd_page_render();
    }
    else
    {
        twr_scheduler_plan_current_relative(500);
    }

    twr_module_lcd_update();
}

void lora_task(void *param)
{
    (void)param;

    if (!twr_cmwx1zzabz_is_ready(&lora))
    {
        twr_scheduler_plan_current_relative(100);

        return;
    }

    static uint8_t buffer[11];

    memset(buffer, 0xff, sizeof(buffer));

    buffer[0] = header;

    if (!isnan(values.battery_voltage))
    {
        buffer[1] = ceil(values.battery_voltage * 10.f);
    }

    if (!isnan(values.temperature))
    {
        int16_t temperature_i16 = (int16_t) (values.temperature * 10.f);

        buffer[2] = temperature_i16;
        buffer[3] = temperature_i16 >> 8;
    }

    if (!isnan(values.humidity))
    {
        buffer[4] = values.humidity * 2;
    }

    if (!isnan(values.pressure))
    {
        uint16_t value = values.pressure / 2.f;
        buffer[5] = value;
        buffer[6] = value >> 8;
    }

    if (!isnan(values.tvoc))
    {
        uint16_t value = values.tvoc;
        buffer[7] = value;
        buffer[8] = value >> 8;
    }

    if (!isnan(values.co2_concentation))
    {
        uint16_t value = values.co2_concentation;
        buffer[9] = value;
        buffer[10] = value >> 8;
    }

    twr_cmwx1zzabz_send_message_confirmed(&lora, buffer, sizeof(buffer));

    static char tmp[sizeof(buffer) * 2 + 1];
    for (size_t i = 0; i < sizeof(buffer); i++)
    {
        sprintf(tmp + i * 2, "%02x", buffer[i]);
    }

    twr_atci_printf("$SEND: %s", tmp);

    header = HEADER_UPDATE;

    twr_scheduler_plan_current_relative(SEND_DATA_INTERVAL);
}
