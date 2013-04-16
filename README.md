== Human date-time parser ==

This repository contains the HumanDateTimeParser class, that can be used to parse user-entered localized and fuzzy date-times.

You can test the class by using the **humandatetime** program. Pass it a single quoted argument containing what you want to parse. For instance, `./humandatetime "two days ago"`.

=== Locale-specific settings ===

Locale-specific parsing can be configured by using XML files. They contain three sections :

* The names of periods (months, years, hours, etc) represented as regular expressions. It is simple in English (a month is a month or months), but other languages can have special names for cultural periods. Every culture-specific period must be mapped to a year, month, week, hour, minute or second. It is possible to have things like a decade that are made of 10 years, or a season of 3 months.
* The named values of periods, for instance the name of the months or the days of the week.
* The parsing rules

Each parsing rule consists of a pattern (it is a full-fledged regular expression), that can contain placeholders like `%period%` (any period) and `%month%` (the name of any period consisting of months, for instance a month or a season), or variables that will be captured by the regular expression. A variable may be `$number$` for any number (made of digits, or "one", "two", etc, localized), or any named value, like `%day%` if you want to match any day name.

See `en_US.xml` for more examples.

When a parsing rule is matched, its variables are captured and available under the `$1`, `$2`,... names. You can use them in **operations**. An operation is an addition, ad substraction and a definition of a time-delta field. For instance, the N in "N days ago" can be applied to a substraction of N days. Again, take a look at `en_US.xml` for examples.

The parsing rules are tried in order, and anything that has been matched against a rule is suppressed from the input, and matching continues. It allows you to write rules matching different part of an input like "Next monday at 8.12 pm". Here, "Next monday" will match a day, "8.12" an hour, and the pm will finally be matched and will add 12 to the hours (hours are represented in 24-hours format).

The operations are also carried out in order, so you can program imperatively using them. They are not turing-complete, though ;-) .
