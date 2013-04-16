/* This file is part of the HumanDateTime parser
   Copyright (c) 2013 Denis Steckelmacher <steckdenis@yahoo.fr>

   This library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Library General Public
   License version 2 as published by the Free Software Foundation,
   or any later version.

   This library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Library General Public License for more details.

   You should have received a copy of the GNU Library General Public License
   along with this library; see the file COPYING.LIB.  If not, write to
   the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
   Boston, MA 02110-1301, USA.
*/

#include "humandatetimeparser.h"

#include <kdebug.h>
#include <KDE/KLocale>
#include <KDE/KCalendarSystem>
#include <KDE/KCalendarSystemFactory>

#include <QVector>
#include <QHash>
#include <QRegExp>
#include <QFile>
#include <QTime>

#include <QDomDocument>
#include <QDomElement>

// 173 debug area : kdecore/KLocale
#define AREA 173

namespace {

/**
 * @brief Period (day, month, hour, etc)
 */
struct Period
{
    enum Type
    {
        Second = 0,
        Minute,
        Hour,
        Day,
        Week,
        Month,
        Year,
        Invalid
    };

    Type type;          /*!< @brief Type of the period */
    int value;          /*!< @brief Number of real periods in this period (a decade is 10 years) */
    QString regexp;     /*!< @brief Regular expression used to parse this period */

    static Type typeForName(const QString &name) {
        if (name == "second") {
            return Second;
        } else if (name == "minute") {
            return Minute;
        } else if (name == "hour") {
            return Hour;
        } else if (name == "day") {
            return Day;
        } else if (name == "week") {
            return Week;
        } else if (name == "month") {
            return Month;
        } else if (name == "year") {
            return Year;
        } else {
            return Invalid;
        }
    }
};

/**
 * @brief Set of names. With one of these name, we have a period and a value (a Set operation)
 */
struct Value
{
    QString name;                   /*!< @brief Names of the values */
    QHash<QString, int> values;     /*!< @brief Values that this value can take (february is the second month of the year) */

    QString regex() const
    {
        QString rs;

        Q_FOREACH(const QString &key, values.keys()) {
            if (!rs.isEmpty()) {
                rs.append('|');
            }

            rs.append(key);
        }

        if (name == "number") {
            // Also parse raw numbers
            rs.append(QLatin1String("|\\d+"));
        }

        return rs;
    }

    int value(const QString &s) {
        // Parse numbers directly
        bool ok;
        int rs = s.toInt(&ok);

        if (ok) {
            return rs;
        }

        // Get the corresponding value
        return values.value(s, 0);
    }
};

/**
 * @brief Operation carried when a rule matches
 */
struct Operation
{
    enum Type
    {
        Add,
        Sub,
        Set
    };

    Type type;              /*!< @brief Type of operation */
    int value;              /*!< @brief Value of the operation (add 3 days), ignored if a %number was parsed */
    int capture_index;      /*!< @brief Index of the capture providing the value of this operation, -1 if value has to be used */

    Period *period;         /*!< @brief Period which will receive the value, Period::Invalid if parsed from %period */

    static Type typeForName(const QString &name) {
        if (name == "add") {
            return Add;
        } else if (name == "sub") {
            return Sub;
        } else if (name == "set") {
            return Set;
        } else {
            kDebug(AREA) << "Unknown period name" << name;
            return Add;
        }
    }
};

/**
 * @brief Parsing rule
 */
struct Rule
{
    QRegExp regexp;                     /*!< @brief Regular expression used for matching */
    Period *period;                     /*!< @brief Period that will be matched */

    QVector<Value *> values;            /*!< @brief List of values captured by this */
    QVector<Operation *> operations;    /*!< @brief Operations to carry when this rule matches */
};

/**
 * @brief Field of a date-time delta
 */
struct Field
{
    Field()
    : value(0),
      set(false),
      relative(true)
    {}

    int value;      /*!< @brief Value of the field */
    bool set;       /*!< @brief Are we setting the field or adding it an offset ? (set 14 pm, or add 1 hour to current hour) */
    bool relative;  /*!< @brief Relative delta, or absolute value (first day of week, or one day ago) */
};

/**
 * @brief Date-time delta
 */
struct DateTimeDelta
{
    Field fields[Period::Invalid];
};

} // namespace

struct HumanDateTimeParser::Private
{
    KLocale *locale;                /*!< @brief Locale to be used for parsing */
    QRegExp wordSplit;              /*!< @brief Regular expression used to split words (this regular expression is replaced by a space in the input text) */
    bool caseSensitive;             /*!< @brief Case-sensitive matching. If false, everything is converted to lowercase */

    // Periods understood by the parser
    QVector<Period *> periods;
    QVector<Value *> values;
    QVector<Operation *> operations;
    QVector<Rule *> rules;

    // Methods
    QHash<QString, Period *> loadPeriods(const QDomDocument &doc);
    QHash<QString, Value *> loadValues(const QDomDocument &doc);
    void loadRules(const QDomDocument &doc,
                   const QHash<QString, Period *> &period_hash,
                   const QHash<QString, Value *> &value_hash);
};

/*
 * HumanDateTimeParser
 */
HumanDateTimeParser::HumanDateTimeParser(KLocale *locale)
: d(new Private)
{
    d->locale = locale;

    // Open the XML file for the language
    QFile fl(locale->language() + ".xml");
    QDomDocument doc;

    if (!fl.open(QIODevice::ReadOnly)) {
        kDebug(AREA) << "Unable to open the locale file" << fl.fileName();
        return;
    } else {
        doc.setContent(fl.readAll());
        fl.close();
    }

    // Read the metadata
    QDomElement language = doc.documentElement();

    d->wordSplit = QRegExp(language.attribute("wordsplit"));
    d->caseSensitive = (language.attribute("casesensitive") == "true");

    // Read the periods
    QHash<QString, Period *> period_hash = d->loadPeriods(doc);

    // Read the name sets
    QHash<QString, Value *> value_hash = d->loadValues(doc);

    // Read the rules
    d->loadRules(doc, period_hash, value_hash);
}

HumanDateTimeParser::~HumanDateTimeParser()
{
    qDeleteAll(d->rules);
    qDeleteAll(d->operations);
    qDeleteAll(d->values);
    qDeleteAll(d->periods);

    delete d;
}

static void _handle_time(QDateTime &time, const Field &f, int multiplier, bool h, bool m, bool s)
{
    if (f.relative) {
        time = time.addSecs(f.value * multiplier);
    } else if (f.set) {
        time.setTime(QTime(
            h ? f.value : time.time().hour(),
            m ? f.value : time.time().minute(),
            s ? f.value : time.time().second()
        ));
    }
}

QDateTime HumanDateTimeParser::parse(const QString &string, const QDateTime &reference)
{
    DateTimeDelta delta;

    // Prepare the string for parsing
    QString s(string);

    s.replace(d->wordSplit, " ");

    if (!d->caseSensitive) {
        s = s.toLower();
    }

    s = " " + s + " ";

    // Try to match rules
    Q_FOREACH(Rule *rule, d->rules) {
        QRegExp regexp = rule->regexp;
        int index = regexp.indexIn(s);

        if (index >= 0) {
            // Apply the operations
            Q_FOREACH(Operation *op, rule->operations) {
                // Get the field
                Period *period =
                    (op->period ? op->period : rule->period);
                Field &field = delta.fields[period->type];

                // Get the value
                int value;

                if (op->capture_index >= 0) {
                    Value *capture = rule->values.at(op->capture_index - 1);    // capture_index starts at 1, not 0
                    value = capture->value(regexp.cap(op->capture_index));
                } else {
                    value = op->value;
                }

                value *= period->value;

                // Apply the operation
                switch (op->type) {
                    case Operation::Add:
                        field.value += value;
                        field.relative = true;
                        break;
                    case Operation::Sub:
                        field.value -= value;
                        field.relative = true;
                        break;
                    case Operation::Set:
                        field.value = value;
                        field.relative = false;
                        break;
                }

                field.set = true;
            }

            // Remove the matching part from the string
            s.remove(index, regexp.matchedLength() - 1);    // -1, don't remove the final space
        }
    }

    // Begin with the current date-time
    QDate cur_date = reference.date();
    KCalendarSystem *cal = KCalendarSystem::create(d->locale->calendarSystem(), d->locale);

    // Day & week
    {
        Field &d = delta.fields[Period::Day];
        Field &w = delta.fields[Period::Week];
        Field &m = delta.fields[Period::Month];
        Field &y = delta.fields[Period::Year];

        // Year
        if (y.relative) {
            cur_date = cal->addYears(cur_date, y.value);
        } else if (y.set) {
            cal->setYMD(cur_date, y.value, cal->month(cur_date), cal->day(cur_date));
        }

        // Month
        if (m.relative) {
            cur_date = cal->addMonths(cur_date, m.value);
        } else if (m.set) {
            cal->setYMD(cur_date, cal->year(cur_date), m.value, cal->day(cur_date));
        }

        // Week
        if (w.set) {
            if (w.relative) {
                cur_date = cal->addDays(cur_date, w.value * 7);
            } else if (y.set) {     // Week of year
                cal->setDateIsoWeek(cur_date, cal->year(cur_date), w.value, cal->dayOfWeek(cur_date));
            } else {                // Week of month
                cal->setYMD(cur_date, cal->year(cur_date), cal->month(cur_date), 1);
                cal->setDateIsoWeek(cur_date, cal->year(cur_date), cal->week(cur_date) + w.value, 1);
            }
        }

        // Day of month or day of week
        if (d.set) {
            if (d.relative) {
                cur_date = cal->addDays(cur_date, d.value);
            } else if (m.set) {             // Day in month
                cal->setYMD(cur_date, cal->year(cur_date), cal->month(cur_date), d.value);
            } else if (w.set || !y.set) {   // Day in week
                cal->setDateIsoWeek(cur_date, cal->year(cur_date), cal->week(cur_date), d.value);
            } else {                        // Day in year
                cal->setDate(cur_date, cal->year(cur_date), d.value);
            }
        }
    }

    // Time
    QDateTime rs(cur_date, reference.time());

    _handle_time(rs, delta.fields[Period::Hour], 60 * 60, true, false, false);
    _handle_time(rs, delta.fields[Period::Minute], 60, false, true, false);
    _handle_time(rs, delta.fields[Period::Second], 1, false, false, true);

    return rs;
}

QHash<QString, Period *> HumanDateTimeParser::Private::loadPeriods(const QDomDocument& doc)
{
    QDomElement period = doc.documentElement().firstChildElement("period");
    QHash<QString, Period *> period_hash;
    Period *p;

    while (!period.isNull()) {
        p = new Period;

        p->type = p->typeForName(period.attribute("type"));
        p->value = period.attribute("value", "1").toInt();
        p->regexp = period.text();

        period_hash.insertMulti(period.attribute("type"), p);
        periods.append(p);

        period = period.nextSiblingElement("period");
    }

    return period_hash;
}

QHash<QString, Value *> HumanDateTimeParser::Private::loadValues(const QDomDocument& doc)
{
    QDomElement values = doc.documentElement().firstChildElement("values");
    QHash<QString, Value *> value_hash;
    Value *v;

    while (!values.isNull()) {
        v = new Value;

        v->name = values.attribute("name");

        // Parse the names
        QDomElement value = values.firstChildElement("value");

        while (!value.isNull()) {
            v->values.insert(
                value.text(),
                value.attribute("value").toInt()
            );

            value = value.nextSiblingElement("value");
        }

        value_hash.insert(v->name, v);
        this->values.append(v);

        values = values.nextSiblingElement("values");
    }

    return value_hash;
}

void HumanDateTimeParser::Private::loadRules(const QDomDocument& doc,
                                             const QHash<QString, Period *> &period_hash,
                                             const QHash<QString, Value *> &value_hash)
{
    // Read the rules
    QDomElement rule = doc.documentElement().firstChildElement("rule");
    Rule *r;

    while (!rule.isNull()) {
        // Parse the operations of the rule
        QDomElement operation = rule.firstChildElement();
        QVector<Operation *> operations;
        Operation *o;

        while (!operation.isNull()) {
            QString val = operation.attribute("value", "$1");
            o = new Operation;

            o->period = period_hash.value(operation.attribute("period"));
            o->type = Operation::typeForName(operation.tagName());

            if (val.startsWith('$')) {
                o->value = 0;
                o->capture_index = val.mid(1).toInt();
            } else {
                o->capture_index = -1;
                o->value = val.toInt();
            }

            operations.append(o);
            this->operations.append(o);

            operation = operation.nextSiblingElement();
        }

        // Parse the pattern, develop it if it contains a "%period%" part
        QString pattern = " " + rule.attribute("pattern") + " ";
        QString regex;

        bool in_value = false;
        QString value_text;

        QVector<Period *> periods;
        QVector<Value *> values;
        int period_insert = -1;

        for (int i = 0; i<pattern.size(); ++i) {
            if (pattern[i] == '%') {
                if (!in_value) {
                    // Begin capturing a period
                    period_insert = regex.size();
                } else {
                    // We just ended a period
                    if (value_text == "period") {
                        periods = this->periods;
                    } else {
                        periods = period_hash.values(value_text).toVector();
                    }
                }

                in_value = !in_value;
            }
            else if (pattern[i] == '$') {
                if (in_value) {
                    Value *val = value_hash.value(value_text);

                    values.append(val);
                    regex.append("(" + val->regex() + ")");
                }

                in_value = !in_value;
            }
            else if (in_value && pattern[i].isLetter()) {
                // Add a character to a capture text
                value_text.append(pattern[i]);
            } else {
                // Add the character to the regular expression
                regex.append(pattern[i]);
                value_text.clear();
            }
        }

        if (periods.count() == 0) {
            periods.append(NULL);   // Even if we don't match any period, the rule must be saved
        }

        // Develop the pattern for each period
        Q_FOREACH(Period *period, periods) {
            QString regexp(regex);
            r = new Rule;

            r->period = period;
            r->operations = operations;
            r->values = values;

            // Finalize the regex for this period
            if (period && period_insert >= 0) {
                regexp.insert(period_insert, period->regexp);
            }

            r->regexp = QRegExp(regexp, Qt::CaseSensitive, QRegExp::RegExp2);

            rules.append(r);
        }

        rule = rule.nextSiblingElement("rule");
    }
}
