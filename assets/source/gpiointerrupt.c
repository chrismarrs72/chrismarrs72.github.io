
/*
 *  ======== gpiointerrupt.c ========
 */
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* Driver Header files */
#include <ti/drivers/GPIO.h>
#include <ti/drivers/UART.h>
#include <ti/drivers/I2C.h>
#include <ti/drivers/Timer.h>

/* Driver configuration */
#include "ti_drivers_config.h"

/* Defines */
#define CONFIG_GPIO_LED_RED CONFIG_GPIO_LED_0
#define DISPLAY(x) UART_write(uart, &output, x);

// Task priorities
#define PRIORITY_HIGH 3
#define PRIORITY_MEDIUM 2
#define PRIORITY_LOW 1

// Constants for readability
#define DEFAULT_TEMP 25
#define DEFAULT_SETPOINT 22
#define BUTTON_CHECK_PERIOD 200
#define TEMP_CHECK_PERIOD 500
#define DISPLAY_UPDATE_PERIOD 1000
#define TIMER_PERIOD 100

// Global variables
int16_t currentTemp = DEFAULT_TEMP;  // Initial room temperature
int16_t setPoint = DEFAULT_SETPOINT; // Default set-point temperature
bool heatOn = false;
char output[64];

// Driver Handles - Global variables
UART_Handle uart;
I2C_Handle i2c;
Timer_Handle timer0;
volatile unsigned char TimerFlag = 0;
volatile unsigned long currentTime = 0;

// Task Data Struct
typedef struct {
    char taskName[20];
    int priority;
    unsigned long lastExecutedTimestamp;
    unsigned long period;
    void (*taskFunction)(void);
} Task;

Task taskQueue[10];
int taskCount = 0;

// Sensor Data Struct
typedef struct {
    uint8_t address;
    uint8_t resultReg;
    char *id;
} Sensor;

// Sensor information
Sensor sensors[] = {
    { 0x48, 0x0000, "11X" },
    { 0x49, 0x0000, "116" },
    { 0x41, 0x0001, "006" }
};

uint8_t txBuffer[1];
uint8_t rxBuffer[2];
I2C_Transaction i2cTransaction;

// Button flags
bool leftButtonPressed = false;
bool rightButtonPressed = false;

// History Data Struct
typedef struct {
    unsigned long seconds;
    int currentTemp;
    int setPoint;
    int heatOn;
} History;

/*
 * ======== Function Prototypes ========
 */
void initUART(void);
void initI2C(void);
void initTimer(void);
void checkButtons(void);
int16_t readTemperature(void);
void controlHeating(void);
void updateDisplay(void);
void timerCallback(Timer_Handle myHandle, int_fast16_t status);
void gpioButtonFxn0(uint_least8_t index);
void gpioButtonFxn1(uint_least8_t index);

/*
 * ======== Task Management ========
 */
// Add a task to the priority queue
void addTask(char *name, int priority, unsigned long period, void (*taskFunction)(void)) {
    strcpy(taskQueue[taskCount].taskName, name);
    taskQueue[taskCount].priority = priority;
    taskQueue[taskCount].lastExecutedTimestamp = 0;
    taskQueue[taskCount].period = period;
    taskQueue[taskCount].taskFunction = taskFunction;
    taskCount++;
}

// Compare tasks by priority for sorting
int compareTasks(const void *a, const void *b) {
    return ((Task *)b)->priority - ((Task *)a)->priority;
}

// Execute tasks based on priority and time intervals
void executeTasks(unsigned long currentTime) {
    int i = 0;
    for (i = 0; i < taskCount; i++) {
        if ((currentTime - taskQueue[i].lastExecutedTimestamp) >= taskQueue[i].period) {
            taskQueue[i].taskFunction();
            taskQueue[i].lastExecutedTimestamp = currentTime;
        }
    }
}


/*
 * ======== Initialization Functions ========
 */

// UART Initialization
void initUART(void)
{
    UART_Params uartParams;
    UART_init();
    UART_Params_init(&uartParams);
    uartParams.baudRate = 115200;
    uart = UART_open(CONFIG_UART_0, &uartParams);

    if (uart == NULL) {
        while (1);  // UART_open() failed
    }
}

// Make sure you call initUART() before calling this function.
void initI2C(void)
{
    int8_t  i, found;
    I2C_Params  i2cParams;

    DISPLAY(snprintf(output, 64, "Initializing I2C Driver - "))

    // Init the driver
    I2C_init();

    // Configure the driver
    I2C_Params_init(&i2cParams);
    i2cParams.bitRate = I2C_400kHz;

    // Open the driver
    i2c = I2C_open(CONFIG_I2C_0, &i2cParams);
    if (i2c == NULL)
    {
        DISPLAY(snprintf(output, 64, "Failed\n\r"))
        while (1);
    }

    DISPLAY(snprintf(output, 32, "Passed\n\r"))

    // Boards were shipped with different sensors.
    // Welcome to the world of embedded systems.
    // Try to determine which sensor we have.
    // Scan through the possible sensor addresses

    /* Common I2C transaction setup */
    i2cTransaction.writeBuf   = txBuffer;
    i2cTransaction.writeCount = 1;
    i2cTransaction.readBuf    = rxBuffer;
    i2cTransaction.readCount  = 0;

    found = false;
    for (i=0; i<3; ++i)
    {
        i2cTransaction.slaveAddress = sensors[i].address;
        txBuffer[0] = sensors[i].resultReg;

        DISPLAY(snprintf(output, 64, "Is this %s? ", sensors[i].id))
        if (I2C_transfer(i2c, &i2cTransaction))
        {
            DISPLAY(snprintf(output, 64, "Found\n\r"))
            found = true;
            break;
        }
        DISPLAY(snprintf(output, 64, "No\n\r"))
    }

    if(found)
    {
        DISPLAY(snprintf(output, 64, "Detected TMP%s I2C address: %x\n\r", sensors[i].id, i2cTransaction.slaveAddress))
    } else {
        DISPLAY(snprintf(output, 64, "Temperature sensor not found, contact professor\n\r"))
    }
}

// Timer Initialization
void initTimer(void)
{
    Timer_Params params;
    Timer_init();
    Timer_Params_init(&params);
    params.period = TIMER_PERIOD * 1000;  // Timer period in microseconds
    params.periodUnits = Timer_PERIOD_US;
    params.timerMode = Timer_CONTINUOUS_CALLBACK;
    params.timerCallback = timerCallback;
    timer0 = Timer_open(CONFIG_TIMER_0, &params);

    if (timer0 == NULL) {
        while (1);  // Failed to initialize timer
    }

    if (Timer_start(timer0) == Timer_STATUS_ERROR) {
        while (1);  // Failed to start timer
    }
}

/*
 * ======== Button Handlers ========
 */

// Left button callback (decrease set point)
void gpioButtonFxn0(uint_least8_t index)
{
    leftButtonPressed = true;
}

// Right button callback (increase set point)
void gpioButtonFxn1(uint_least8_t index)
{
    rightButtonPressed = true;
}

// Check button state and update set point
void checkButtons(void)
{
    if (rightButtonPressed) {
        setPoint += 1;
        rightButtonPressed = false;
    }
    if (leftButtonPressed) {
        setPoint -= 1;
        leftButtonPressed = false;
    }
}

/*
 * ======== Temperature Sensor Handling ========
 */

int16_t readTemperature(void)
{
    int16_t temperature = 0;
    i2cTransaction.readCount = 2;
    if (I2C_transfer(i2c, &i2cTransaction)) {
        temperature = (rxBuffer[0] << 8) | (rxBuffer[1]);
        temperature *= 0.0078125;
        if (rxBuffer[0] & 0x80) {
            temperature |= 0xF000;
        }
    } else {
        DISPLAY(snprintf(output, 64, "Error reading temperature.\n\r"));
    }
    return temperature;
}

/*
 * ======== Heating Control ========
 */

void controlHeating(void)
{
    currentTemp = readTemperature();
    if (currentTemp < setPoint) {
        GPIO_write(CONFIG_GPIO_LED_RED, CONFIG_GPIO_LED_ON);
        heatOn = true;
    } else {
        GPIO_write(CONFIG_GPIO_LED_RED, CONFIG_GPIO_LED_OFF);
        heatOn = false;
    }
}

/*
 * ======== Display Handling ========
 */

void updateDisplay(void)
{
    long seconds = currentTime/(TIMER_PERIOD * 10);
    History histData = { seconds, currentTemp, setPoint, heatOn };  // store for display and database use

    DISPLAY(snprintf(output, 64, "<%02d:%02d, Temperature:%02d, SetPoint:%02d, Heat:%d>\n\r",
                     histData.seconds/60L, histData.seconds%60L, histData.currentTemp, histData.setPoint, histData.heatOn));
}

/*
 * ======== Timer Callback ========
 */

void timerCallback(Timer_Handle myHandle, int_fast16_t status)
{
    TimerFlag = 1;
    currentTime += TIMER_PERIOD;  // Increment current time by timer period (100 ms)
}

/*
 * ======== Main Thread ========
 */

void *mainThread(void *arg0)
{
    initUART();
    initI2C();
    initTimer();

    // Configure buttons and LED
    GPIO_setConfig(CONFIG_GPIO_LED_RED, GPIO_CFG_OUT_STD | GPIO_CFG_OUT_LOW);
    GPIO_setConfig(CONFIG_GPIO_BUTTON_0, GPIO_CFG_IN_PU | GPIO_CFG_IN_INT_FALLING);
    GPIO_setConfig(CONFIG_GPIO_BUTTON_1, GPIO_CFG_IN_PU | GPIO_CFG_IN_INT_FALLING);

    GPIO_setCallback(CONFIG_GPIO_BUTTON_0, gpioButtonFxn0);
    GPIO_setCallback(CONFIG_GPIO_BUTTON_1, gpioButtonFxn1);
    GPIO_enableInt(CONFIG_GPIO_BUTTON_0);
    GPIO_enableInt(CONFIG_GPIO_BUTTON_1);

    // Add tasks to the queue
    addTask("Check Buttons", PRIORITY_HIGH, BUTTON_CHECK_PERIOD, checkButtons);
    addTask("Monitor Temperature", PRIORITY_MEDIUM, TEMP_CHECK_PERIOD, controlHeating);
    addTask("Update Display", PRIORITY_LOW, DISPLAY_UPDATE_PERIOD, updateDisplay);
    // Sort tasks by priority
    qsort(taskQueue, taskCount, sizeof(Task), compareTasks);

    while (1) {
        if (TimerFlag) {
            executeTasks(currentTime);
            TimerFlag = 0;  // Reset TimerFlag
        }
    }
}
