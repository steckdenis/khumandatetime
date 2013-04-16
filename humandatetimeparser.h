#ifndef __HUMANDATETIMEPARSER_H__
#define __HUMANDATETIMEPARSER_H__

#include <QString>
#include <QDateTime>

#include <KDE/KGlobal>

class KLocale;

/**
 * @brief Parse a human-entered datetime
 */
class HumanDateTimeParser
{
    public:
        HumanDateTimeParser(KLocale *locale = KGlobal::locale());
        ~HumanDateTimeParser();

        QDateTime parse(const QString &string, const QDateTime &reference = QDateTime::currentDateTime());

    private:
        struct Private;
        Private *d;
};

#endif