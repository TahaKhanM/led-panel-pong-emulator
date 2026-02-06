#include "libopencm3/stm32/rcc.h"  //Needed to enable the clock
#include "libopencm3/stm32/gpio.h" //Needed to define things on the GPIO
#include "libopencm3/stm32/adc.h"  //Needed to convert analogue signals to digital
#include <unistd.h>

#define LEDPANEL_PORT GPIOC

#define A_PIN GPIO2
#define B_PIN GPIO3
#define C_PIN GPIO4
#define D_PIN GPIO5
#define INP_PIN GPIO6
#define CLK_PIN GPIO7
#define LAT_PIN GPIO8
#define IOPORT GPIOA
#define JOYSTICK_A_PORT GPIOA
#define JOYSTICK_B_PORT GPIOC
#define ADC_REG ADC1

uint32_t getRawInput(int channelValue);
void delay_ms(uint32_t ms); // assume 1Mhz clock
void PrepareLatch(void);
void LatchRegister(void);
void SelectRow(int row);
void PushBit(int onoff);
void ClearRow(int row);
void setupPanel(void);
void setupInput(void);
 

void delay_ms(uint32_t ms) // assume 1Mhz clock
{
  for (volatile unsigned int tmr = ms; tmr > 0; tmr--)
    __asm__("nop");
}
void PrepareLatch(void)
{
  gpio_clear(LEDPANEL_PORT, LAT_PIN);
}

void LatchRegister(void)
{
  // set the latch pin, which will display what is in the register
  gpio_set(LEDPANEL_PORT, LAT_PIN);
}

void SelectRow(int row)
{
  if (row % 2 == 1)
  {
    gpio_set(LEDPANEL_PORT, A_PIN);
  }
  else
  {
    gpio_clear(LEDPANEL_PORT, A_PIN);
  }
  row /= 2;
  if (row % 2 == 1)
  {
    gpio_set(LEDPANEL_PORT, B_PIN);
  }
  else
  {
    gpio_clear(LEDPANEL_PORT, B_PIN);
  }
  row /= 2;
  if (row % 2 == 1)
  {
    gpio_set(LEDPANEL_PORT, C_PIN);
  }
  else
  {
    gpio_clear(LEDPANEL_PORT, C_PIN);
  }
  row /= 2;
  if (row % 2 == 1)
  {
    gpio_set(LEDPANEL_PORT, D_PIN);
  }
  else
  {
    gpio_clear(LEDPANEL_PORT, D_PIN);
  }
}

void PushBit(int onoff)
{
  // clear the clock, push a 1 or 0, and set the clock to push it in
  gpio_clear(LEDPANEL_PORT, CLK_PIN);

  if (onoff)
    gpio_set(LEDPANEL_PORT, INP_PIN);
  else
    gpio_clear(LEDPANEL_PORT, INP_PIN);

  gpio_set(LEDPANEL_PORT, CLK_PIN);
}

void ClearRow(int row)
{
  SelectRow(row);
  // 192 bits: 2 panel halfs, 3 bits per pixel, 32 pixels per half
  for (int b = 0; b < 192; b++)
  {
    PushBit(0);
  }
}

void setupPanel()
{
  rcc_periph_clock_enable(RCC_GPIOA); // Enable clock
  rcc_periph_clock_enable(RCC_GPIOC); // Enable clock

  // On board LED
  gpio_mode_setup(GPIOA, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, GPIO5);
  gpio_set_output_options(GPIOA, GPIO_OTYPE_PP, GPIO_OSPEED_100MHZ, GPIO5);

  // A Pin
  gpio_mode_setup(LEDPANEL_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, A_PIN);
  gpio_set_output_options(LEDPANEL_PORT, GPIO_OTYPE_PP, GPIO_OSPEED_100MHZ, A_PIN);

  // B Pin
  gpio_mode_setup(LEDPANEL_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, B_PIN);
  gpio_set_output_options(LEDPANEL_PORT, GPIO_OTYPE_PP, GPIO_OSPEED_100MHZ, B_PIN);

  // C Pin
  gpio_mode_setup(LEDPANEL_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, C_PIN);
  gpio_set_output_options(LEDPANEL_PORT, GPIO_OTYPE_PP, GPIO_OSPEED_100MHZ, C_PIN);

  // D Pin
  gpio_mode_setup(LEDPANEL_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, D_PIN);
  gpio_set_output_options(LEDPANEL_PORT, GPIO_OTYPE_PP, GPIO_OSPEED_100MHZ, D_PIN);

  // CLK Pin
  gpio_mode_setup(LEDPANEL_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, CLK_PIN);
  gpio_set_output_options(LEDPANEL_PORT, GPIO_OTYPE_PP, GPIO_OSPEED_100MHZ, CLK_PIN);

  // Input Pin
  gpio_mode_setup(LEDPANEL_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, INP_PIN);
  gpio_set_output_options(LEDPANEL_PORT, GPIO_OTYPE_PP, GPIO_OSPEED_100MHZ, INP_PIN);

  // Latch Pin
  gpio_mode_setup(LEDPANEL_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, LAT_PIN);
  gpio_set_output_options(LEDPANEL_PORT, GPIO_OTYPE_PP, GPIO_OSPEED_100MHZ, LAT_PIN);
}

// Function to configure GPIO registers
void setupInput()
{
  rcc_periph_clock_enable(RCC_ADC12); // Enable clock for ADC registers 1 and 2

  // Setting up adc register 1 --> we require only one of the registers since both the joysticks are connected to 1 register
  adc_power_off(ADC_REG); // Turn off ADC register 1 whist we set it up

  adc_set_clk_prescale(ADC_REG, ADC_CCR_CKMODE_DIV1);                   // Setup a scaling, none is fine for this
  adc_disable_external_trigger_regular(ADC_REG);                        // We don't need to externally trigger the register...
  adc_set_right_aligned(ADC_REG);                                       // Make sure it is right aligned to get more usable values
  adc_set_sample_time_on_all_channels(ADC_REG, ADC_SMPR_SMP_61DOT5CYC); // Set up sample time
  adc_set_resolution(ADC_REG, ADC_CFGR1_RES_12_BIT);                    // Get a good resolution

  adc_power_on(ADC_REG); // Finished setup, turn on ADC register 1
}

uint32_t getRawInput(int channelValue)
{                                                     // For setting up channels for each direction
  uint8_t channelArray[1] = {channelValue};           // Define a channel that we want to look at
  adc_set_regular_sequence(ADC_REG, 1, channelArray); // Set up the channel
  adc_start_conversion_regular(ADC_REG);              // Start converting the analogue signal

  while (!(adc_eoc(ADC_REG)))
    ; // Wait until the register is ready to read data

  uint32_t value = adc_read_regular(ADC_REG); // Read the value from the register and channel
  return value;
}