#include "humandatetimeparser.h"

#include <QtDebug>

int main(int argc, char **argv)
{
    if (argc != 2)
        return 0;

    HumanDateTimeParser parser;

    qDebug() << parser.parse(argv[1]);
}