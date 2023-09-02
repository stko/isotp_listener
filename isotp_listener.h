#ifndef ISOTP_LISTENER_H
#define ISOTP_LISTENER_H

class Isotp_Listener
{
private:
    int m_year;
    int m_month;
    int m_day;

public:
    Isotp_Listener(int year, int month, int day);

    void SetDate(int year, int month, int day);

    int getYear() { return m_year; }
    int getMonth() { return m_month; }
    int getDay()  { return m_day; }
};

#endif