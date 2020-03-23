#include <application.h>

#define BATTERY_UPDATE_INTERVAL (60 * 60 * 1000)

#define TMP112_PUB_NO_CHANGE_INTERVAL (15 * 60 * 1000)
#define TMP112_PUB_VALUE_CHANGE 0.2f
#define TMP112_UPDATE_INTERVAL (2 * 1000)

#define VOC_LP_TAG_UPDATE_INTERVAL (5 * 1000)
#define VOC_LP_TAG_PUB_VALUE_CHANGE 5.0f
#define VOC_LP_TAG_PUB_NO_CHANGE_INTERVAL (15 * 60 * 1000)

#define HUMIDITY_TAG_PUB_NO_CHANGE_INTERVAL (15 * 60 * 1000)
#define HUMIDITY_TAG_PUB_VALUE_CHANGE 5.0f
#define HUMIDITY_TAG_UPDATE_INTERVAL (2 * 1000)

#define BAROMETER_TAG_PUB_NO_CHANGE_INTERVAL (15 * 60 * 1000)
#define BAROMETER_TAG_PUB_VALUE_CHANGE 10.0f
#define BAROMETER_TAG_UPDATE_INTERVAL (1 * 60 * 1000)

#define CO2_PUB_NO_CHANGE_INTERVAL (15 * 60 * 1000)
#define CO2_PUB_VALUE_CHANGE 50.0f

#define MAX_PAGE_INDEX 3

#define PAGE_INDEX_MENU -1
#define CO2_UPDATE_INTERVAL (1 * 60 * 1000)

bc_led_t led;
bool led_state = false;

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
    bc_tick_t next_update;
    bool mqtt;

} lcd;

void battery_event_handler(bc_module_battery_event_t event, void *event_param);

static void lcd_page_render();
static void humidity_tag_init(bc_tag_humidity_revision_t revision, bc_i2c_channel_t i2c_channel, humidity_tag_t *tag);
static void barometer_tag_init(bc_i2c_channel_t i2c_channel, barometer_tag_t *tag);

void lcd_event_handler(bc_module_lcd_event_t event, void *event_param);
void tmp112_event_handler(bc_tmp112_t *self, bc_tmp112_event_t event, void *event_param);
void humidity_tag_event_handler(bc_tag_humidity_t *self, bc_tag_humidity_event_t event, void *event_param);
void barometer_tag_event_handler(bc_tag_barometer_t *self, bc_tag_barometer_event_t event, void *event_param);
void co2_event_handler(bc_module_co2_event_t event, void *event_param);

void application_init(void)
{
    bc_led_init(&led, BC_GPIO_LED, false, false);
    bc_led_set_mode(&led, BC_LED_MODE_OFF);

    bc_radio_init(BC_RADIO_MODE_NODE_SLEEPING);

    //----------------------------

    static bc_tmp112_t temperature;
    bc_tmp112_init(&temperature, BC_I2C_I2C0, 0x49);
    bc_tmp112_set_event_handler(&temperature, tmp112_event_handler, NULL);
    bc_tmp112_set_update_interval(&temperature, TMP112_UPDATE_INTERVAL);

    //----------------------------

    static humidity_tag_t humidity_tag_0_0;
    humidity_tag_init(BC_TAG_HUMIDITY_REVISION_R1, BC_I2C_I2C0, &humidity_tag_0_0);

    static humidity_tag_t humidity_tag_0_2;
    humidity_tag_init(BC_TAG_HUMIDITY_REVISION_R2, BC_I2C_I2C0, &humidity_tag_0_2);

    static humidity_tag_t humidity_tag_0_4;
    humidity_tag_init(BC_TAG_HUMIDITY_REVISION_R3, BC_I2C_I2C0, &humidity_tag_0_4);

    static humidity_tag_t humidity_tag_1_0;
    humidity_tag_init(BC_TAG_HUMIDITY_REVISION_R1, BC_I2C_I2C1, &humidity_tag_1_0);

    static humidity_tag_t humidity_tag_1_2;
    humidity_tag_init(BC_TAG_HUMIDITY_REVISION_R2, BC_I2C_I2C1, &humidity_tag_1_2);

    static humidity_tag_t humidity_tag_1_4;
    humidity_tag_init(BC_TAG_HUMIDITY_REVISION_R3, BC_I2C_I2C1, &humidity_tag_1_4);

    //----------------------------

    static barometer_tag_t barometer_tag_0_0;
    barometer_tag_init(BC_I2C_I2C0, &barometer_tag_0_0);

    static barometer_tag_t barometer_tag_1_0;
    barometer_tag_init(BC_I2C_I2C1, &barometer_tag_1_0);

    //----------------------------

    static event_param_t co2_event_param = { .next_pub = 0 };
    bc_module_co2_init();
    bc_module_co2_set_update_interval(CO2_UPDATE_INTERVAL);
    bc_module_co2_set_event_handler(co2_event_handler, &co2_event_param);

    //----------------------------

    memset(&values, 0xff, sizeof(values));
    bc_module_lcd_init();
    bc_module_lcd_set_event_handler(lcd_event_handler, NULL);

    bc_module_battery_init();

    bc_module_battery_set_event_handler(battery_event_handler, NULL);
    bc_module_battery_set_update_interval(BATTERY_UPDATE_INTERVAL);

    bc_radio_pairing_request("air-quality-monitor", VERSION);

    bc_led_pulse(&led, 2000);
}

void application_task(void)
{
    if (!bc_module_lcd_is_ready())
    {
        return;
    }

    if (!lcd.mqtt)
    {
        lcd_page_render();
    }
    else
    {
        bc_scheduler_plan_current_relative(500);
    }

    bc_module_lcd_update();
}

static void lcd_page_render()
{

    int w;
    char str[32];

    bc_system_pll_enable();

    bc_module_lcd_clear();

    if ((page_index <= MAX_PAGE_INDEX) && (page_index != PAGE_INDEX_MENU))
    {
        bc_module_lcd_set_font(&bc_font_ubuntu_15);
        bc_module_lcd_draw_string(10, 5, pages[page_index].name0, true);

        bc_module_lcd_set_font(&bc_font_ubuntu_28);
        snprintf(str, sizeof(str), pages[page_index].format0, *pages[page_index].value0);
        w = bc_module_lcd_draw_string(25, 25, str, true);
        bc_module_lcd_set_font(&bc_font_ubuntu_15);
        w = bc_module_lcd_draw_string(w, 35, pages[page_index].unit0, true);

        bc_module_lcd_set_font(&bc_font_ubuntu_15);
        bc_module_lcd_draw_string(10, 55, pages[page_index].name1, true);

        bc_module_lcd_set_font(&bc_font_ubuntu_28);
        snprintf(str, sizeof(str), pages[page_index].format1, *pages[page_index].value1);
        w = bc_module_lcd_draw_string(25, 75, str, true);
        bc_module_lcd_set_font(&bc_font_ubuntu_15);
        bc_module_lcd_draw_string(w, 85, pages[page_index].unit1, true);
    }

    snprintf(str, sizeof(str), "%d/%d", page_index + 1, MAX_PAGE_INDEX + 1);
    bc_module_lcd_set_font(&bc_font_ubuntu_13);
    bc_module_lcd_draw_string(55, 115, str, true);

    bc_system_pll_disable();
}

static void humidity_tag_init(bc_tag_humidity_revision_t revision, bc_i2c_channel_t i2c_channel, humidity_tag_t *tag)
{
    memset(tag, 0, sizeof(*tag));

    if (revision == BC_TAG_HUMIDITY_REVISION_R1)
    {
        tag->param.channel = BC_RADIO_PUB_CHANNEL_R1_I2C0_ADDRESS_DEFAULT;
    }
    else if (revision == BC_TAG_HUMIDITY_REVISION_R2)
    {
        tag->param.channel = BC_RADIO_PUB_CHANNEL_R2_I2C0_ADDRESS_DEFAULT;
    }
    else if (revision == BC_TAG_HUMIDITY_REVISION_R3)
    {
        tag->param.channel = BC_RADIO_PUB_CHANNEL_R3_I2C0_ADDRESS_DEFAULT;
    }
    else
    {
        return;
    }

    if (i2c_channel == BC_I2C_I2C1)
    {
        tag->param.channel |= 0x80;
    }

    bc_tag_humidity_init(&tag->self, revision, i2c_channel, BC_TAG_HUMIDITY_I2C_ADDRESS_DEFAULT);

    bc_tag_humidity_set_update_interval(&tag->self, HUMIDITY_TAG_UPDATE_INTERVAL);

    bc_tag_humidity_set_event_handler(&tag->self, humidity_tag_event_handler, &tag->param);
}

static void barometer_tag_init(bc_i2c_channel_t i2c_channel, barometer_tag_t *tag)
{
    memset(tag, 0, sizeof(*tag));

    tag->param.channel = BC_RADIO_PUB_CHANNEL_R1_I2C0_ADDRESS_DEFAULT;

    bc_tag_barometer_init(&tag->self, i2c_channel);

    bc_tag_barometer_set_update_interval(&tag->self, BAROMETER_TAG_UPDATE_INTERVAL);

    bc_tag_barometer_set_event_handler(&tag->self, barometer_tag_event_handler, &tag->param);
}

void lcd_event_handler(bc_module_lcd_event_t event, void *event_param)
{
    (void) event_param;

    if (event == BC_MODULE_LCD_EVENT_LEFT_CLICK)
    {
        if ((page_index != PAGE_INDEX_MENU))
        {
            // Key prew page
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
        bc_radio_pub_event_count(BC_RADIO_PUB_EVENT_LCD_BUTTON_LEFT, &left_event_count);
    }
    else if(event == BC_MODULE_LCD_EVENT_RIGHT_CLICK)
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
        bc_radio_pub_event_count(BC_RADIO_PUB_EVENT_LCD_BUTTON_RIGHT, &right_event_count);
    }

    bc_scheduler_plan_now(0);
}


void voc_lp_tag_event_handler(bc_tag_voc_lp_t *self, bc_tag_voc_lp_event_t event, void *event_param)
{
    event_param_t *param = (event_param_t *)event_param;

    if (event == BC_TAG_VOC_LP_EVENT_UPDATE)
    {
        uint16_t value;

        if (bc_tag_voc_lp_get_tvoc_ppb(self, &value))
        {
            if ((fabsf(value - param->value) >= VOC_LP_TAG_PUB_VALUE_CHANGE) || (param->next_pub < bc_scheduler_get_spin_tick()))
            {
                param->value = value;
                param->next_pub = bc_scheduler_get_spin_tick() + VOC_LP_TAG_PUB_NO_CHANGE_INTERVAL;

                int radio_tvoc = value;

                values.tvoc = radio_tvoc;

                bc_radio_pub_int("voc-lp-sensor/0:0/tvoc", &radio_tvoc);
                bc_scheduler_plan_now(0);

            }
        }
    }
}

void tmp112_event_handler(bc_tmp112_t *self, bc_tmp112_event_t event, void *event_param)
{
    float value;
    event_param_t *param = (event_param_t *)event_param;

    if (event != BC_TMP112_EVENT_UPDATE)
    {
        return;
    }

    if (bc_tmp112_get_temperature_celsius(self, &value))
    {
        if ((fabs(value - param->value) >= TMP112_PUB_VALUE_CHANGE) || (param->next_pub < bc_scheduler_get_spin_tick()))
        {
            bc_radio_pub_temperature(param->channel, &value);
            param->value = value;
            param->next_pub = bc_scheduler_get_spin_tick() + TMP112_PUB_NO_CHANGE_INTERVAL;

            values.temperature = value;
            bc_scheduler_plan_now(0);
        }
    }
}

void humidity_tag_event_handler(bc_tag_humidity_t *self, bc_tag_humidity_event_t event, void *event_param)
{
    float value;
    event_param_t *param = (event_param_t *)event_param;

    if (event != BC_TAG_HUMIDITY_EVENT_UPDATE)
    {
        return;
    }

    if (bc_tag_humidity_get_humidity_percentage(self, &value))
    {
        if ((fabs(value - param->value) >= HUMIDITY_TAG_PUB_VALUE_CHANGE) || (param->next_pub < bc_scheduler_get_spin_tick()))
        {
            bc_radio_pub_humidity(param->channel, &value);
            param->value = value;
            param->next_pub = bc_scheduler_get_spin_tick() + HUMIDITY_TAG_PUB_NO_CHANGE_INTERVAL;

            values.humidity = value;
            bc_scheduler_plan_now(0);
        }
    }
}

void barometer_tag_event_handler(bc_tag_barometer_t *self, bc_tag_barometer_event_t event, void *event_param)
{
    float pascal;
    float meter;
    event_param_t *param = (event_param_t *)event_param;

    if (event != BC_TAG_BAROMETER_EVENT_UPDATE)
    {
        return;
    }

    if (!bc_tag_barometer_get_pressure_pascal(self, &pascal))
    {
        return;
    }

    if ((fabs(pascal - param->value) >= BAROMETER_TAG_PUB_VALUE_CHANGE) || (param->next_pub < bc_scheduler_get_spin_tick()))
    {

        if (!bc_tag_barometer_get_altitude_meter(self, &meter))
        {
            return;
        }

        bc_radio_pub_barometer(param->channel, &pascal, &meter);
        param->value = pascal;
        param->next_pub = bc_scheduler_get_spin_tick() + BAROMETER_TAG_PUB_NO_CHANGE_INTERVAL;

        values.pressure = pascal / 100.0;
        values.altitude = meter;
        bc_scheduler_plan_now(0);
    }
}

void co2_event_handler(bc_module_co2_event_t event, void *event_param)
{
    event_param_t *param = (event_param_t *) event_param;
    float value;

    if (event == BC_MODULE_CO2_EVENT_UPDATE)
    {
        if (bc_module_co2_get_concentration_ppm(&value))
        {
            if ((fabs(value - param->value) >= CO2_PUB_VALUE_CHANGE) || (param->next_pub < bc_scheduler_get_spin_tick()))
            {
                bc_radio_pub_co2(&value);
                param->value = value;
                param->next_pub = bc_scheduler_get_spin_tick() + CO2_PUB_NO_CHANGE_INTERVAL;

                values.co2_concentation = value;
                bc_scheduler_plan_now(0);
            }
        }
    }
}

void battery_event_handler(bc_module_battery_event_t event, void *event_param)
{
    (void) event;
    (void) event_param;

    float voltage;
    int percentage;

    bc_module_battery_measure();

    if (bc_module_battery_get_voltage(&voltage))
    {
        values.battery_voltage = voltage;
        bc_radio_pub_battery(&voltage);
    }

    if (bc_module_battery_get_charge_level(&percentage))
    {
        values.battery_pct = percentage;
    }
}