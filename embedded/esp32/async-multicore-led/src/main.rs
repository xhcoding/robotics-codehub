#![no_std]
#![no_main]

use embassy_executor::Spawner;
use embassy_sync::blocking_mutex::raw::CriticalSectionRawMutex;
use embassy_sync::signal::Signal;
use embassy_time::{Duration, Ticker};
use esp_backtrace as _;
use esp_hal::system::Stack;
use esp_hal::system::Cpu;
use esp_hal::gpio::{Level, Output, OutputConfig};
use esp_hal::interrupt::software::SoftwareInterruptControl;
use esp_hal::timer::timg::TimerGroup;
use esp_println::{println, logger};
use esp_rtos::embassy::Executor;
use static_cell::StaticCell;

esp_bootloader_esp_idf::esp_app_desc!();

#[embassy_executor::task]
async fn control_led(mut led: Output<'static>, control: &'static Signal<CriticalSectionRawMutex, bool>) {
    println!("Starting control_led() on core {}", Cpu::current() as usize);
    loop {
        if control.wait().await {
            println!("LED on");
            led.set_low();
        } else {
            println!("LED off");
            led.set_high();
        }
    }
}

#[esp_rtos::main]
async fn main(_spawner: Spawner) {
    println!("Init!");
    logger::init_logger_from_env();
    let peripherals = esp_hal::init(esp_hal::Config::default());

    static APP_CORE_STACK: StaticCell<Stack<8192>> = StaticCell::new();
    let app_core_stack = APP_CORE_STACK.init(Stack::new());

    let sw_int = SoftwareInterruptControl::new(peripherals.SW_INTERRUPT);
    let timg0 = TimerGroup::new(peripherals.TIMG0);
    esp_rtos::start(timg0.timer0, sw_int.software_interrupt0);

    static LED_CTRL: StaticCell<Signal<CriticalSectionRawMutex, bool>> = StaticCell::new();
    let led_ctrl_signal = &*LED_CTRL.init(Signal::new());

    let led = Output::new(peripherals.GPIO2, Level::Low
        , OutputConfig::default());

    esp_rtos::start_second_core(peripherals.CPU_CTRL, sw_int.software_interrupt1, app_core_stack, move || {
        static EXECUTOR: StaticCell<Executor> = StaticCell::new();
        let executor = EXECUTOR.init(Executor::new());
        executor.run(|spawner|{
            spawner.spawn(control_led(led, led_ctrl_signal).unwrap());
        });
    });

    println!("Starting enable_disable_led() on core {}", Cpu::current() as usize);
    let mut ticker = Ticker::every(Duration::from_secs(1));
    loop {
        println!("Sending LED on");
        led_ctrl_signal.signal(true);
        ticker.next().await;
        
        println!("Sending LED off");
        led_ctrl_signal.signal(false);
        ticker.next().await;
    }
    
}
