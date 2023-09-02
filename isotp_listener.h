#ifndef ISOTP_LISTENER_H
#define ISOTP_LISTENER_H

class Isotp_Listener
{
private:
    int physical_address;
    int functional_address;
    int(*send_telegram)(int,unsigned char[8],int len);

public:
    Isotp_Listener(int physical_address, int functional_address, int(*send_telegram)(int,unsigned char[8],int len));
    void tick(int time_ticks);
};

#endif