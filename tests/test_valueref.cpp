// Copyright (c) 2026 The QtDMM developers
// SPDX-License-Identifier: GPL-3.0-or-later
// ValueRef: which value of which meter a view follows; stored as the bare
// number for the own meter (old settings), "id@source" for another one.
#include <QDebug>
#include <QJsonObject>
#include <QSet>

#include "core/valueref.h"
#include "testutil.h"

static Reading reading(int id, const QString &source)
{
  Reading r;
  r.id = id;
  r.source = source;
  return r;
}

int main()
{
  // text: the own meter's value is the number it always was
  check(ValueRef(1).toString() == "1", "own meter: bare number");
  check(ValueRef(2, "device:ab12cd34").toString() == "2@device:ab12cd34", "another meter: id@source");
  for (const ValueRef &v : { ValueRef(), ValueRef(3), ValueRef(1, "device:ab12cd34"), ValueRef(0, "instance:bench") })
    check(ValueRef::fromString(v.toString()) == v, "round trip: " + v.toString());
  check(ValueRef::fromString("2") == ValueRef(2), "an old setting (2) reads as the own meter's value 2");
  check(ValueRef::fromString(" 1 @ device:x ") == ValueRef(1, "device:x"), "blanks around the parts");
  for (const char *bad : { "", "x", "1.5", "@device:x" })
    check(ValueRef::fromString(bad) == ValueRef(), QString("unreadable '%1': the main value").arg(bad));
  check(ValueRef::fromString("-1") == ValueRef(0) && ValueRef::fromString("7") == ValueRef(3)
          && ValueRef::fromString("9@device:x") == ValueRef(3, "device:x"),
        "an id beyond the values: bounded, as the settings always did");

  // JSON (alarms): a number as before, text only for another meter
  check(ValueRef(1).toJson().isDouble() && ValueRef(1).toJson().toInt() == 1, "JSON: own meter as number");
  check(ValueRef(1, "device:x").toJson().toString() == "1@device:x", "JSON: another meter as text");
  check(ValueRef::fromJson(QJsonValue(2)) == ValueRef(2), "JSON: an old alarm's number");
  check(ValueRef::fromJson(QJsonValue("1@device:x")) == ValueRef(1, "device:x"), "JSON: text");
  check(ValueRef::fromJson(QJsonObject()["missing"]) == ValueRef(), "JSON: missing is the main value");

  // which readings it follows
  check(ValueRef(1).matches(reading(1, "instance:default")) && ValueRef(1).matches(reading(1, "device:x")),
        "own meter (no source): any reading with the id");
  check(!ValueRef(1).matches(reading(0, "instance:default")), "another id does not match");
  check(ValueRef(1, "device:x").matches(reading(1, "device:x")) && !ValueRef(1, "device:x").matches(reading(1, "device:y")),
        "a source matches only its meter");

  // a set of them (the graph's shown values)
  const QSet<ValueRef> shown { ValueRef(0), ValueRef(1), ValueRef(1, "device:x"), ValueRef(1) };
  check(shown.size() == 3 && shown.contains(ValueRef(1, "device:x")) && !shown.contains(ValueRef(1, "device:y")),
        "QSet: equal refs once, source counts");

  if (failed == 0)
    qInfo() << "All ValueRef tests passed.";
  return failed == 0 ? 0 : 1;
}
