project describtion is : 
    his project aims to design and implement a real-time embedded speech recognition system
    using the AVR ATmega32A microcontroller. The system will recognize 8 predefined spoken 
    words from an analog microphone input and activate one of 8 digital outputs (e.g., LEDs)
    corresponding to the detected word. The project demonstrates how signal acquisition, digital 
    signal processing, feature extraction, and lightweight classification can be implemented on a 
    resource-constrained 8-bit microcontroller.

TASK 1 IN project : 
   we want to trigger adc Conversion each 1.25*10^-4 second aka sampling freq is 8khz , so we will make trigger on timer/counter1 match b so we need to config timer on CTC and it toggle or match be every 1.25*10^-4     