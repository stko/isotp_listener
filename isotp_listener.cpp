/*

uds state machine from https://github.com/stko/oobd/blob/master/interface/OOBD/v1/odp_uds.c

*/

#include "isotp_listener.h"

// Date constructor
Isotp_Listener::Isotp_Listener(int year, int month, int day)
{
    SetDate(year, month, day);
}

// Date member function
void Isotp_Listener::SetDate(int year, int month, int day)
{
    m_month = month;
    m_day = day;
    m_year = year;
}