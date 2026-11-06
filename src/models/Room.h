#ifndef ROOM_H
#define ROOM_H

#include <QString>

struct Room {
    qint64 id = 0;
    QString roomName;
    int capacity = 0;
    QString description;
    int x = 0;   // layout position
    int y = 0;
    int width = 200;
    int height = 150;

    bool isValid() const { return id > 0 || !roomName.isEmpty(); }
};

#endif // ROOM_H
