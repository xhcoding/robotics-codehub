#include "component/key.hpp"
#include "component/led.hpp"
#include "system/system.hpp"

using namespace stm32;
using namespace std::chrono_literals;

int main()
{
    System::init();

    Led led(Gpio::Group::C, 13, Led::ActiveMode::LOW);

    Key key(Gpio::Group::A, 10);

    key.begin(System::now().count());

    while (true) {
        auto event = key.update(System::now().count());
        switch (event) {
        case Key::Event::EV_PRESS: {
            led.on();
            break;
        }
        case Key::Event::EV_RELEASE: {
            led.off();
            break;
        }
        case Key::Event::EV_LONG: {
            for (int i = 0; i < 4; i++) {
                led.on();
                System::delay(500ms);
                led.off();
                System::delay(500ms);
            }
            break;
        }
        default:
            break;
        }
        System::delay(1ms);
    }

    return 0;
}
