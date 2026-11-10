#include "lilos.h"

static uint8_t cmos(uint8_t reg) {
    outb(0x70, reg);
    return inb(0x71);
}

static uint8_t bcd(uint8_t v) {
    return (uint8_t)((v & 0x0F) + (v >> 4) * 10);
}

void rtc_read(struct rtc_time *t) {
    while (cmos(0x0A) & 0x80)
        ;

    uint8_t sec = cmos(0x00), min = cmos(0x02), hour = cmos(0x04);
    uint8_t day = cmos(0x07), month = cmos(0x08), year = cmos(0x09);
    uint8_t mode = cmos(0x0B);
    int pm = hour & 0x80;

    hour &= 0x7F;
    if (!(mode & 0x04)) {
        sec = bcd(sec); min = bcd(min); hour = bcd(hour);
        day = bcd(day); month = bcd(month); year = bcd(year);
    }
    if (!(mode & 0x02) && pm)
        hour = (uint8_t)((hour + 12) % 24);

    t->sec = sec;
    t->min = min;
    t->hour = hour;
    t->day = day;
    t->month = month;
    t->year = (uint16_t)(2000 + year);
}
