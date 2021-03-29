/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * vim: set ts=8 sts=2 et sw=2 tw=80:
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* Intl.DateTimeFormat implementation. */

#include "builtin/intl/DateTimeFormat.h"

#include "mozilla/Assertions.h"
#include "mozilla/Range.h"
#include "mozilla/Span.h"

#include "jsfriendapi.h"

#include "builtin/Array.h"
#include "builtin/intl/CommonFunctions.h"
#include "builtin/intl/LanguageTag.h"
#include "builtin/intl/ScopedICUObject.h"
#include "builtin/intl/SharedIntlData.h"
#include "builtin/intl/TimeZoneDataGenerated.h"
#include "gc/FreeOp.h"
#include "js/CharacterEncoding.h"
#include "js/Date.h"
#include "js/experimental/Intl.h"     // JS::AddMozDateTimeFormatConstructor
#include "js/friend/ErrorMessages.h"  // js::GetErrorMessage, JSMSG_*
#include "js/GCAPI.h"
#include "js/PropertySpec.h"
#include "js/StableStringChars.h"
#include "unicode/ucal.h"
#include "unicode/udat.h"
#include "unicode/udateintervalformat.h"
#include "unicode/udatpg.h"
#include "unicode/uenum.h"
#include "unicode/ufieldpositer.h"
#include "unicode/uloc.h"
#include "unicode/utypes.h"
#include "vm/DateTime.h"
#include "vm/GlobalObject.h"
#include "vm/JSContext.h"
#include "vm/PlainObject.h"  // js::PlainObject
#include "vm/Runtime.h"
#include "vm/WellKnownAtom.h"  // js_*_str

#include "vm/JSObject-inl.h"
#include "vm/NativeObject-inl.h"

using namespace js;

using JS::AutoStableStringChars;
using JS::ClippedTime;
using JS::TimeClip;

using js::intl::CallICU;
using js::intl::DateTimeFormatOptions;
using js::intl::IcuLocale;
using js::intl::INITIAL_CHAR_BUFFER_SIZE;
using js::intl::SharedIntlData;
using js::intl::StringsAreEqual;

const JSClassOps DateTimeFormatObject::classOps_ = {
    nullptr,                         // addProperty
    nullptr,                         // delProperty
    nullptr,                         // enumerate
    nullptr,                         // newEnumerate
    nullptr,                         // resolve
    nullptr,                         // mayResolve
    DateTimeFormatObject::finalize,  // finalize
    nullptr,                         // call
    nullptr,                         // hasInstance
    nullptr,                         // construct
    nullptr,                         // trace
};

const JSClass DateTimeFormatObject::class_ = {
    "Intl.DateTimeFormat",
    JSCLASS_HAS_RESERVED_SLOTS(DateTimeFormatObject::SLOT_COUNT) |
        JSCLASS_HAS_CACHED_PROTO(JSProto_DateTimeFormat) |
        JSCLASS_FOREGROUND_FINALIZE,
    &DateTimeFormatObject::classOps_, &DateTimeFormatObject::classSpec_};

const JSClass& DateTimeFormatObject::protoClass_ = PlainObject::class_;

static bool dateTimeFormat_toSource(JSContext* cx, unsigned argc, Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);
  args.rval().setString(cx->names().DateTimeFormat);
  return true;
}

static const JSFunctionSpec dateTimeFormat_static_methods[] = {
    JS_SELF_HOSTED_FN("supportedLocalesOf",
                      "Intl_DateTimeFormat_supportedLocalesOf", 1, 0),
    JS_FS_END};

static const JSFunctionSpec dateTimeFormat_methods[] = {
    JS_SELF_HOSTED_FN("resolvedOptions", "Intl_DateTimeFormat_resolvedOptions",
                      0, 0),
    JS_SELF_HOSTED_FN("formatToParts", "Intl_DateTimeFormat_formatToParts", 1,
                      0),
#ifdef NIGHTLY_BUILD
#  ifndef U_HIDE_DRAFT_API
    JS_SELF_HOSTED_FN("formatRange", "Intl_DateTimeFormat_formatRange", 2, 0),
    JS_SELF_HOSTED_FN("formatRangeToParts",
                      "Intl_DateTimeFormat_formatRangeToParts", 2, 0),
#  endif
#endif
    JS_FN(js_toSource_str, dateTimeFormat_toSource, 0, 0),
    JS_FS_END};

static const JSPropertySpec dateTimeFormat_properties[] = {
    JS_SELF_HOSTED_GET("format", "$Intl_DateTimeFormat_format_get", 0),
    JS_STRING_SYM_PS(toStringTag, "Intl.DateTimeFormat", JSPROP_READONLY),
    JS_PS_END};

static bool DateTimeFormat(JSContext* cx, unsigned argc, Value* vp);

const ClassSpec DateTimeFormatObject::classSpec_ = {
    GenericCreateConstructor<DateTimeFormat, 0, gc::AllocKind::FUNCTION>,
    GenericCreatePrototype<DateTimeFormatObject>,
    dateTimeFormat_static_methods,
    nullptr,
    dateTimeFormat_methods,
    dateTimeFormat_properties,
    nullptr,
    ClassSpec::DontDefineConstructor};

/**
 * 12.2.1 Intl.DateTimeFormat([ locales [, options]])
 *
 * ES2017 Intl draft rev 94045d234762ad107a3d09bb6f7381a65f1a2f9b
 */
static bool DateTimeFormat(JSContext* cx, const CallArgs& args, bool construct,
                           DateTimeFormatOptions dtfOptions) {
  // Step 1 (Handled by OrdinaryCreateFromConstructor fallback code).

  // Step 2 (Inlined 9.1.14, OrdinaryCreateFromConstructor).
  JSProtoKey protoKey = dtfOptions == DateTimeFormatOptions::Standard
                            ? JSProto_DateTimeFormat
                            : JSProto_Null;
  RootedObject proto(cx);
  if (!GetPrototypeFromBuiltinConstructor(cx, args, protoKey, &proto)) {
    return false;
  }

  Rooted<DateTimeFormatObject*> dateTimeFormat(cx);
  dateTimeFormat = NewObjectWithClassProto<DateTimeFormatObject>(cx, proto);
  if (!dateTimeFormat) {
    return false;
  }

  RootedValue thisValue(
      cx, construct ? ObjectValue(*dateTimeFormat) : args.thisv());
  HandleValue locales = args.get(0);
  HandleValue options = args.get(1);

  // Step 3.
  return intl::LegacyInitializeObject(
      cx, dateTimeFormat, cx->names().InitializeDateTimeFormat, thisValue,
      locales, options, dtfOptions, args.rval());
}

static bool DateTimeFormat(JSContext* cx, unsigned argc, Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);
  return DateTimeFormat(cx, args, args.isConstructing(),
                        DateTimeFormatOptions::Standard);
}

static bool MozDateTimeFormat(JSContext* cx, unsigned argc, Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);

  // Don't allow to call mozIntl.DateTimeFormat as a function. That way we
  // don't need to worry how to handle the legacy initialization semantics
  // when applied on mozIntl.DateTimeFormat.
  if (!ThrowIfNotConstructing(cx, args, "mozIntl.DateTimeFormat")) {
    return false;
  }

  return DateTimeFormat(cx, args, true,
                        DateTimeFormatOptions::EnableMozExtensions);
}

bool js::intl_DateTimeFormat(JSContext* cx, unsigned argc, Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);
  MOZ_ASSERT(args.length() == 2);
  MOZ_ASSERT(!args.isConstructing());
  // intl_DateTimeFormat is an intrinsic for self-hosted JavaScript, so it
  // cannot be used with "new", but it still has to be treated as a
  // constructor.
  return DateTimeFormat(cx, args, true, DateTimeFormatOptions::Standard);
}

void js::DateTimeFormatObject::finalize(JSFreeOp* fop, JSObject* obj) {
  MOZ_ASSERT(fop->onMainThread());

  auto* dateTimeFormat = &obj->as<DateTimeFormatObject>();
  UDateFormat* df = dateTimeFormat->getDateFormat();
  UDateIntervalFormat* dif = dateTimeFormat->getDateIntervalFormat();

  if (df) {
    intl::RemoveICUCellMemory(
        fop, obj, DateTimeFormatObject::UDateFormatEstimatedMemoryUse);

    udat_close(df);
  }

  if (dif) {
    intl::RemoveICUCellMemory(
        fop, obj, DateTimeFormatObject::UDateIntervalFormatEstimatedMemoryUse);

    udtitvfmt_close(dif);
  }
}

bool JS::AddMozDateTimeFormatConstructor(JSContext* cx,
                                         JS::Handle<JSObject*> intl) {
  RootedObject ctor(
      cx, GlobalObject::createConstructor(cx, MozDateTimeFormat,
                                          cx->names().DateTimeFormat, 0));
  if (!ctor) {
    return false;
  }

  RootedObject proto(
      cx, GlobalObject::createBlankPrototype<PlainObject>(cx, cx->global()));
  if (!proto) {
    return false;
  }

  if (!LinkConstructorAndPrototype(cx, ctor, proto)) {
    return false;
  }

  // 12.3.2
  if (!JS_DefineFunctions(cx, ctor, dateTimeFormat_static_methods)) {
    return false;
  }

  // 12.4.4 and 12.4.5
  if (!JS_DefineFunctions(cx, proto, dateTimeFormat_methods)) {
    return false;
  }

  // 12.4.2 and 12.4.3
  if (!JS_DefineProperties(cx, proto, dateTimeFormat_properties)) {
    return false;
  }

  RootedValue ctorValue(cx, ObjectValue(*ctor));
  return DefineDataProperty(cx, intl, cx->names().DateTimeFormat, ctorValue, 0);
}

static bool DefaultCalendar(JSContext* cx, const UniqueChars& locale,
                            MutableHandleValue rval) {
  UErrorCode status = U_ZERO_ERROR;
  UCalendar* cal = ucal_open(nullptr, 0, locale.get(), UCAL_DEFAULT, &status);

  // This correctly handles nullptr |cal| when opening failed.
  ScopedICUObject<UCalendar, ucal_close> closeCalendar(cal);

  const char* calendar = ucal_getType(cal, &status);
  if (U_FAILURE(status)) {
    intl::ReportInternalError(cx);
    return false;
  }

  // ICU returns old-style keyword values; map them to BCP 47 equivalents
  calendar = uloc_toUnicodeLocaleType("ca", calendar);
  if (!calendar) {
    intl::ReportInternalError(cx);
    return false;
  }

  JSString* str = NewStringCopyZ<CanGC>(cx, calendar);
  if (!str) {
    return false;
  }

  rval.setString(str);
  return true;
}

struct CalendarAlias {
  const char* const calendar;
  const char* const alias;
};

const CalendarAlias calendarAliases[] = {{"islamic-civil", "islamicc"},
                                         {"ethioaa", "ethiopic-amete-alem"}};

bool js::intl_availableCalendars(JSContext* cx, unsigned argc, Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);
  MOZ_ASSERT(args.length() == 1);
  MOZ_ASSERT(args[0].isString());

  UniqueChars locale = intl::EncodeLocale(cx, args[0].toString());
  if (!locale) {
    return false;
  }

  RootedObject calendars(cx, NewDenseEmptyArray(cx));
  if (!calendars) {
    return false;
  }

  // We need the default calendar for the locale as the first result.
  RootedValue defaultCalendar(cx);
  if (!DefaultCalendar(cx, locale, &defaultCalendar)) {
    return false;
  }

  if (!NewbornArrayPush(cx, calendars, defaultCalendar)) {
    return false;
  }

  // Now get the calendars that "would make a difference", i.e., not the
  // default.
  UErrorCode status = U_ZERO_ERROR;
  UEnumeration* values =
      ucal_getKeywordValuesForLocale("ca", locale.get(), false, &status);
  if (U_FAILURE(status)) {
    intl::ReportInternalError(cx);
    return false;
  }
  ScopedICUObject<UEnumeration, uenum_close> toClose(values);

  uint32_t count = uenum_count(values, &status);
  if (U_FAILURE(status)) {
    intl::ReportInternalError(cx);
    return false;
  }

  for (; count > 0; count--) {
    const char* calendar = uenum_next(values, nullptr, &status);
    if (U_FAILURE(status)) {
      intl::ReportInternalError(cx);
      return false;
    }

    // ICU returns old-style keyword values; map them to BCP 47 equivalents
    calendar = uloc_toUnicodeLocaleType("ca", calendar);
    if (!calendar) {
      intl::ReportInternalError(cx);
      return false;
    }

    JSString* jscalendar = NewStringCopyZ<CanGC>(cx, calendar);
    if (!jscalendar) {
      return false;
    }
    if (!NewbornArrayPush(cx, calendars, StringValue(jscalendar))) {
      return false;
    }

    // ICU doesn't return calendar aliases, append them here.
    for (const auto& calendarAlias : calendarAliases) {
      if (StringsAreEqual(calendar, calendarAlias.calendar)) {
        JSString* jscalendar = NewStringCopyZ<CanGC>(cx, calendarAlias.alias);
        if (!jscalendar) {
          return false;
        }
        if (!NewbornArrayPush(cx, calendars, StringValue(jscalendar))) {
          return false;
        }
      }
    }
  }

  args.rval().setObject(*calendars);
  return true;
}

bool js::intl_defaultCalendar(JSContext* cx, unsigned argc, Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);
  MOZ_ASSERT(args.length() == 1);
  MOZ_ASSERT(args[0].isString());

  UniqueChars locale = intl::EncodeLocale(cx, args[0].toString());
  if (!locale) {
    return false;
  }

  return DefaultCalendar(cx, locale, args.rval());
}

bool js::intl_IsValidTimeZoneName(JSContext* cx, unsigned argc, Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);
  MOZ_ASSERT(args.length() == 1);
  MOZ_ASSERT(args[0].isString());

  SharedIntlData& sharedIntlData = cx->runtime()->sharedIntlData.ref();

  RootedString timeZone(cx, args[0].toString());
  RootedAtom validatedTimeZone(cx);
  if (!sharedIntlData.validateTimeZoneName(cx, timeZone, &validatedTimeZone)) {
    return false;
  }

  if (validatedTimeZone) {
    cx->markAtom(validatedTimeZone);
    args.rval().setString(validatedTimeZone);
  } else {
    args.rval().setNull();
  }

  return true;
}

bool js::intl_canonicalizeTimeZone(JSContext* cx, unsigned argc, Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);
  MOZ_ASSERT(args.length() == 1);
  MOZ_ASSERT(args[0].isString());

  SharedIntlData& sharedIntlData = cx->runtime()->sharedIntlData.ref();

  // Some time zone names are canonicalized differently by ICU -- handle
  // those first:
  RootedString timeZone(cx, args[0].toString());
  RootedAtom ianaTimeZone(cx);
  if (!sharedIntlData.tryCanonicalizeTimeZoneConsistentWithIANA(
          cx, timeZone, &ianaTimeZone)) {
    return false;
  }

  if (ianaTimeZone) {
    cx->markAtom(ianaTimeZone);
    args.rval().setString(ianaTimeZone);
    return true;
  }

  AutoStableStringChars stableChars(cx);
  if (!stableChars.initTwoByte(cx, timeZone)) {
    return false;
  }

  mozilla::Range<const char16_t> tzchars = stableChars.twoByteRange();

  JSString* str = CallICU(cx, [&tzchars](UChar* chars, uint32_t size,
                                         UErrorCode* status) {
    return ucal_getCanonicalTimeZoneID(tzchars.begin().get(), tzchars.length(),
                                       chars, size, nullptr, status);
  });
  if (!str) {
    return false;
  }

  args.rval().setString(str);
  return true;
}

bool js::intl_defaultTimeZone(JSContext* cx, unsigned argc, Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);
  MOZ_ASSERT(args.length() == 0);

  // The current default might be stale, because JS::ResetTimeZone() doesn't
  // immediately update ICU's default time zone. So perform an update if
  // needed.
  js::ResyncICUDefaultTimeZone();

  JSString* str = CallICU(cx, ucal_getDefaultTimeZone);
  if (!str) {
    return false;
  }

  args.rval().setString(str);
  return true;
}

bool js::intl_defaultTimeZoneOffset(JSContext* cx, unsigned argc, Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);
  MOZ_ASSERT(args.length() == 0);

  UErrorCode status = U_ZERO_ERROR;
  const UChar* uTimeZone = nullptr;
  int32_t uTimeZoneLength = 0;
  const char* rootLocale = "";
  UCalendar* cal =
      ucal_open(uTimeZone, uTimeZoneLength, rootLocale, UCAL_DEFAULT, &status);
  if (U_FAILURE(status)) {
    intl::ReportInternalError(cx);
    return false;
  }
  ScopedICUObject<UCalendar, ucal_close> toClose(cal);

  int32_t offset = ucal_get(cal, UCAL_ZONE_OFFSET, &status);
  if (U_FAILURE(status)) {
    intl::ReportInternalError(cx);
    return false;
  }

  args.rval().setInt32(offset);
  return true;
}

bool js::intl_isDefaultTimeZone(JSContext* cx, unsigned argc, Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);
  MOZ_ASSERT(args.length() == 1);
  MOZ_ASSERT(args[0].isString() || args[0].isUndefined());

  // |undefined| is the default value when the Intl runtime caches haven't
  // yet been initialized. Handle it the same way as a cache miss.
  if (args[0].isUndefined()) {
    args.rval().setBoolean(false);
    return true;
  }

  // The current default might be stale, because JS::ResetTimeZone() doesn't
  // immediately update ICU's default time zone. So perform an update if
  // needed.
  js::ResyncICUDefaultTimeZone();

  Vector<char16_t, INITIAL_CHAR_BUFFER_SIZE> chars(cx);
  MOZ_ALWAYS_TRUE(chars.resize(INITIAL_CHAR_BUFFER_SIZE));

  int32_t size = CallICU(cx, ucal_getDefaultTimeZone, chars);
  if (size < 0) {
    return false;
  }

  JSLinearString* str = args[0].toString()->ensureLinear(cx);
  if (!str) {
    return false;
  }

  bool equals;
  if (str->length() == size_t(size)) {
    JS::AutoCheckCannotGC nogc;
    equals =
        str->hasLatin1Chars()
            ? EqualChars(str->latin1Chars(nogc), chars.begin(), str->length())
            : EqualChars(str->twoByteChars(nogc), chars.begin(), str->length());
  } else {
    equals = false;
  }

  args.rval().setBoolean(equals);
  return true;
}

enum class HourCycle {
  // 12 hour cycle, from 0 to 11.
  H11,

  // 12 hour cycle, from 1 to 12.
  H12,

  // 24 hour cycle, from 0 to 23.
  H23,

  // 24 hour cycle, from 1 to 24.
  H24
};

static bool IsHour12(HourCycle hc) {
  return hc == HourCycle::H11 || hc == HourCycle::H12;
}

static char16_t HourSymbol(HourCycle hc) {
  switch (hc) {
    case HourCycle::H11:
      return 'K';
    case HourCycle::H12:
      return 'h';
    case HourCycle::H23:
      return 'H';
    case HourCycle::H24:
      return 'k';
  }
  MOZ_MAKE_COMPILER_ASSUME_IS_UNREACHABLE("unexpected hour cycle");
}

/**
 * Parse a pattern according to the format specified in
 * <https://unicode.org/reports/tr35/tr35-dates.html#Date_Format_Patterns>.
 */
template <typename CharT>
class PatternIterator {
  CharT* iter_;
  const CharT* const end_;

 public:
  explicit PatternIterator(mozilla::Span<CharT> pattern)
      : iter_(pattern.data()), end_(pattern.data() + pattern.size()) {}

  CharT* next() {
    MOZ_ASSERT(iter_ != nullptr);

    bool inQuote = false;
    while (iter_ < end_) {
      CharT* cur = iter_++;
      if (*cur == '\'') {
        inQuote = !inQuote;
      } else if (!inQuote) {
        return cur;
      }
    }

    iter_ = nullptr;
    return nullptr;
  }
};

/**
 * Return the hour cycle for the given option string.
 */
static HourCycle HourCycleFromOption(JSLinearString* str) {
  if (StringEqualsLiteral(str, "h11")) {
    return HourCycle::H11;
  }
  if (StringEqualsLiteral(str, "h12")) {
    return HourCycle::H12;
  }
  if (StringEqualsLiteral(str, "h23")) {
    return HourCycle::H23;
  }
  MOZ_ASSERT(StringEqualsLiteral(str, "h24"));
  return HourCycle::H24;
}

/**
 * Return the hour cycle used in the input pattern or Nothing if none was found.
 */
template <typename CharT>
static mozilla::Maybe<HourCycle> HourCycleFromPattern(
    mozilla::Span<const CharT> pattern) {
  PatternIterator<const CharT> iter(pattern);
  while (const auto* ptr = iter.next()) {
    switch (*ptr) {
      case 'K':
        return mozilla::Some(HourCycle::H11);
      case 'h':
        return mozilla::Some(HourCycle::H12);
      case 'H':
        return mozilla::Some(HourCycle::H23);
      case 'k':
        return mozilla::Some(HourCycle::H24);
    }
  }
  return mozilla::Nothing();
}

/**
 * Replaces all hour pattern characters in |patternOrSkeleton| to use the
 * matching hour representation for |hourCycle|.
 */
static void ReplaceHourSymbol(mozilla::Span<char16_t> patternOrSkeleton,
                              HourCycle hc) {
  char16_t replacement = HourSymbol(hc);
  PatternIterator<char16_t> iter(patternOrSkeleton);
  while (auto* ptr = iter.next()) {
    char16_t ch = *ptr;
    if (ch == 'K' || ch == 'h' || ch == 'H' || ch == 'k' || ch == 'j') {
      *ptr = replacement;
    }
  }
}

bool js::intl_patternForSkeleton(JSContext* cx, unsigned argc, Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);
  MOZ_ASSERT(args.length() == 3);
  MOZ_ASSERT(args[0].isString());
  MOZ_ASSERT(args[1].isString());
  MOZ_ASSERT(args[2].isString() || args[2].isUndefined());

  UniqueChars locale = intl::EncodeLocale(cx, args[0].toString());
  if (!locale) {
    return false;
  }

  AutoStableStringChars skeleton(cx);
  if (!skeleton.initTwoByte(cx, args[1].toString())) {
    return false;
  }

  mozilla::Maybe<HourCycle> hourCycle;
  if (args[2].isString()) {
    JSLinearString* hourCycleStr = args[2].toString()->ensureLinear(cx);
    if (!hourCycleStr) {
      return false;
    }

    hourCycle.emplace(HourCycleFromOption(hourCycleStr));
  }

  mozilla::Range<const char16_t> skelChars = skeleton.twoByteRange();

  SharedIntlData& sharedIntlData = cx->runtime()->sharedIntlData.ref();
  UDateTimePatternGenerator* gen =
      sharedIntlData.getDateTimePatternGenerator(cx, locale.get());
  if (!gen) {
    return false;
  }

  Vector<char16_t, intl::INITIAL_CHAR_BUFFER_SIZE> pattern(cx);
  MOZ_ALWAYS_TRUE(pattern.resize(intl::INITIAL_CHAR_BUFFER_SIZE));

  int32_t patternSize = CallICU(
      cx,
      [gen, &skelChars](UChar* chars, uint32_t size, UErrorCode* status) {
        return udatpg_getBestPattern(gen, skelChars.begin().get(),
                                     skelChars.length(), chars, size, status);
      },
      pattern);
  if (patternSize < 0) {
    return false;
  }
  pattern.shrinkTo(size_t(patternSize));

  // If the hourCycle option was set, adjust the resolved pattern to use the
  // requested hour cycle representation.
  if (hourCycle) {
    ReplaceHourSymbol(pattern, hourCycle.value());
  }

  JSString* str = NewStringCopyN<CanGC>(cx, pattern.begin(), pattern.length());
  if (!str) {
    return false;
  }
  args.rval().setString(str);
  return true;
}

/**
 * Find a matching pattern using the requested hour-12 options.
 *
 * This function is needed to work around the following two issues.
 * - https://unicode-org.atlassian.net/browse/ICU-21023
 * - https://unicode-org.atlassian.net/browse/CLDR-13425
 *
 * We're currently using a relatively simple workaround, which doesn't give the
 * most accurate results. For example:
 *
 * ```
 * var dtf = new Intl.DateTimeFormat("en", {
 *   timeZone: "UTC",
 *   dateStyle: "long",
 *   timeStyle: "long",
 *   hourCycle: "h12",
 * });
 * print(dtf.format(new Date("2020-01-01T00:00Z")));
 * ```
 *
 * Returns the pattern "MMMM d, y 'at' h:mm:ss a z", but when going through
 * |udatpg_getSkeleton| and then |udatpg_getBestPattern| to find an equivalent
 * pattern for "h23", we'll end up with the pattern "MMMM d, y, HH:mm:ss z", so
 * the combinator element " 'at' " was lost in the process.
 */
template <size_t N>
static bool FindPatternWithHourCycle(JSContext* cx, const char* locale,
                                     Vector<char16_t, N>& pattern,
                                     bool hour12) {
  SharedIntlData& sharedIntlData = cx->runtime()->sharedIntlData.ref();
  UDateTimePatternGenerator* gen =
      sharedIntlData.getDateTimePatternGenerator(cx, locale);
  if (!gen) {
    return false;
  }

  Vector<char16_t, intl::INITIAL_CHAR_BUFFER_SIZE> skeleton(cx);
  MOZ_ALWAYS_TRUE(skeleton.resize(intl::INITIAL_CHAR_BUFFER_SIZE));

  int32_t skeletonSize = CallICU(
      cx,
      [&pattern](UChar* chars, uint32_t size, UErrorCode* status) {
        return udatpg_getSkeleton(nullptr, pattern.begin(), pattern.length(),
                                  chars, size, status);
      },
      skeleton);
  if (skeletonSize < 0) {
    return false;
  }
  skeleton.shrinkTo(size_t(skeletonSize));

  // Input skeletons don't differentiate between "K" and "h" resp. "k" and "H".
  ReplaceHourSymbol(skeleton, hour12 ? HourCycle::H12 : HourCycle::H23);

  MOZ_ALWAYS_TRUE(pattern.resize(N));

  int32_t patternSize = CallICU(
      cx,
      [gen, &skeleton](UChar* chars, uint32_t size, UErrorCode* status) {
        return udatpg_getBestPattern(gen, skeleton.begin(), skeleton.length(),
                                     chars, size, status);
      },
      pattern);
  if (patternSize < 0) {
    return false;
  }
  pattern.shrinkTo(size_t(patternSize));

  return true;
}

bool js::intl_patternForStyle(JSContext* cx, unsigned argc, Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);
  MOZ_ASSERT(args.length() == 6);
  MOZ_ASSERT(args[0].isString());
  MOZ_ASSERT(args[1].isString() || args[1].isUndefined());
  MOZ_ASSERT(args[2].isString() || args[2].isUndefined());
  MOZ_ASSERT(args[3].isString());
  MOZ_ASSERT(args[4].isBoolean() || args[4].isUndefined());
  MOZ_ASSERT(args[5].isString() || args[5].isUndefined());

  UniqueChars locale = intl::EncodeLocale(cx, args[0].toString());
  if (!locale) {
    return false;
  }

  auto toDateFormatStyle = [](JSLinearString* str) {
    if (StringEqualsLiteral(str, "full")) {
      return UDAT_FULL;
    }
    if (StringEqualsLiteral(str, "long")) {
      return UDAT_LONG;
    }
    if (StringEqualsLiteral(str, "medium")) {
      return UDAT_MEDIUM;
    }
    MOZ_ASSERT(StringEqualsLiteral(str, "short"));
    return UDAT_SHORT;
  };

  UDateFormatStyle dateStyle = UDAT_NONE;
  if (args[1].isString()) {
    JSLinearString* dateStyleStr = args[1].toString()->ensureLinear(cx);
    if (!dateStyleStr) {
      return false;
    }

    dateStyle = toDateFormatStyle(dateStyleStr);
  }

  UDateFormatStyle timeStyle = UDAT_NONE;
  if (args[2].isString()) {
    JSLinearString* timeStyleStr = args[2].toString()->ensureLinear(cx);
    if (!timeStyleStr) {
      return false;
    }

    timeStyle = toDateFormatStyle(timeStyleStr);
  }

  AutoStableStringChars timeZone(cx);
  if (!timeZone.initTwoByte(cx, args[3].toString())) {
    return false;
  }

  mozilla::Maybe<bool> hour12;
  if (args[4].isBoolean()) {
    hour12.emplace(args[4].toBoolean());
  }

  mozilla::Maybe<HourCycle> hourCycle;
  if (args[5].isString()) {
    JSLinearString* hourCycleStr = args[5].toString()->ensureLinear(cx);
    if (!hourCycleStr) {
      return false;
    }

    hourCycle.emplace(HourCycleFromOption(hourCycleStr));
  }

  mozilla::Range<const char16_t> timeZoneChars = timeZone.twoByteRange();

  UErrorCode status = U_ZERO_ERROR;
  UDateFormat* df = udat_open(timeStyle, dateStyle, IcuLocale(locale.get()),
                              timeZoneChars.begin().get(),
                              timeZoneChars.length(), nullptr, -1, &status);
  if (U_FAILURE(status)) {
    intl::ReportInternalError(cx);
    return false;
  }
  ScopedICUObject<UDateFormat, udat_close> toClose(df);

  Vector<char16_t, intl::INITIAL_CHAR_BUFFER_SIZE> pattern(cx);
  MOZ_ALWAYS_TRUE(pattern.resize(intl::INITIAL_CHAR_BUFFER_SIZE));

  int32_t patternSize = CallICU(
      cx,
      [df](UChar* chars, uint32_t size, UErrorCode* status) {
        return udat_toPattern(df, false, chars, size, status);
      },
      pattern);
  if (patternSize < 0) {
    return false;
  }
  pattern.shrinkTo(size_t(patternSize));

  // If a specific hour cycle was requested and this hour cycle doesn't match
  // the hour cycle used in the resolved pattern, find an equivalent pattern
  // with the correct hour cycle.
  if (timeStyle != UDAT_NONE && (hour12 || hourCycle)) {
    if (auto hcPattern = HourCycleFromPattern<char16_t>(pattern)) {
      bool wantHour12 = hour12 ? hour12.value() : IsHour12(hourCycle.value());
      if (wantHour12 != IsHour12(hcPattern.value())) {
        if (!FindPatternWithHourCycle(cx, locale.get(), pattern, wantHour12)) {
          return false;
        }
      }
    }
  }

  // If the hourCycle option was set, adjust the resolved pattern to use the
  // requested hour cycle representation.
  if (hourCycle) {
    ReplaceHourSymbol(pattern, hourCycle.value());
  }

  JSString* str = NewStringCopyN<CanGC>(cx, pattern.begin(), pattern.length());
  if (!str) {
    return false;
  }
  args.rval().setString(str);
  return true;
}

bool js::intl_skeletonForPattern(JSContext* cx, unsigned argc, Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);
  MOZ_ASSERT(args.length() == 1);
  MOZ_ASSERT(args[0].isString());

  AutoStableStringChars pattern(cx);
  if (!pattern.initTwoByte(cx, args[0].toString())) {
    return false;
  }
  mozilla::Range<const char16_t> patternChars = pattern.twoByteRange();

  JSString* skeleton = CallICU(
      cx, [&patternChars](char16_t* chars, int32_t size, UErrorCode* status) {
        return udatpg_getSkeleton(nullptr, patternChars.begin().get(),
                                  patternChars.length(), chars, size, status);
      });
  if (!skeleton) {
    return false;
  }

  args.rval().setString(skeleton);
  return true;
}

static UniqueChars DateTimeFormatLocale(
    JSContext* cx, HandleObject internals,
    mozilla::Maybe<HourCycle> hourCycle = mozilla::Nothing()) {
  RootedValue value(cx);
  if (!GetProperty(cx, internals, internals, cx->names().locale, &value)) {
    return nullptr;
  }

  // ICU expects calendar, numberingSystem, and hourCycle as Unicode locale
  // extensions on locale.

  intl::LanguageTag tag(cx);
  {
    JSLinearString* locale = value.toString()->ensureLinear(cx);
    if (!locale) {
      return nullptr;
    }

    if (!intl::LanguageTagParser::parse(cx, locale, tag)) {
      return nullptr;
    }
  }

  JS::RootedVector<intl::UnicodeExtensionKeyword> keywords(cx);

  if (!GetProperty(cx, internals, internals, cx->names().calendar, &value)) {
    return nullptr;
  }

  {
    JSLinearString* calendar = value.toString()->ensureLinear(cx);
    if (!calendar) {
      return nullptr;
    }

    if (!keywords.emplaceBack("ca", calendar)) {
      return nullptr;
    }
  }

  if (!GetProperty(cx, internals, internals, cx->names().numberingSystem,
                   &value)) {
    return nullptr;
  }

  {
    JSLinearString* numberingSystem = value.toString()->ensureLinear(cx);
    if (!numberingSystem) {
      return nullptr;
    }

    if (!keywords.emplaceBack("nu", numberingSystem)) {
      return nullptr;
    }
  }

  if (hourCycle) {
    JSAtom* hourCycleStr;
    switch (*hourCycle) {
      case HourCycle::H11:
        hourCycleStr = cx->names().h11;
        break;
      case HourCycle::H12:
        hourCycleStr = cx->names().h12;
        break;
      case HourCycle::H23:
        hourCycleStr = cx->names().h23;
        break;
      case HourCycle::H24:
        hourCycleStr = cx->names().h24;
        break;
    }

    if (!keywords.emplaceBack("hc", hourCycleStr)) {
      return nullptr;
    }
  }

  // |ApplyUnicodeExtensionToTag| applies the new keywords to the front of
  // the Unicode extension subtag. We're then relying on ICU to follow RFC
  // 6067, which states that any trailing keywords using the same key
  // should be ignored.
  if (!intl::ApplyUnicodeExtensionToTag(cx, tag, keywords)) {
    return nullptr;
  }

  return tag.toStringZ(cx);
}

/**
 * Returns a new UDateFormat with the locale and date-time formatting options
 * of the given DateTimeFormat.
 */
static UDateFormat* NewUDateFormat(
    JSContext* cx, Handle<DateTimeFormatObject*> dateTimeFormat) {
  RootedValue value(cx);

  RootedObject internals(cx, intl::GetInternalsObject(cx, dateTimeFormat));
  if (!internals) {
    return nullptr;
  }

  UniqueChars locale = DateTimeFormatLocale(cx, internals);
  if (!locale) {
    return nullptr;
  }

  if (!GetProperty(cx, internals, internals, cx->names().timeZone, &value)) {
    return nullptr;
  }

  AutoStableStringChars timeZone(cx);
  if (!timeZone.initTwoByte(cx, value.toString())) {
    return nullptr;
  }

  mozilla::Range<const char16_t> timeZoneChars = timeZone.twoByteRange();

  if (!GetProperty(cx, internals, internals, cx->names().pattern, &value)) {
    return nullptr;
  }

  AutoStableStringChars pattern(cx);
  if (!pattern.initTwoByte(cx, value.toString())) {
    return nullptr;
  }

  mozilla::Range<const char16_t> patternChars = pattern.twoByteRange();

  UErrorCode status = U_ZERO_ERROR;
  UDateFormat* df =
      udat_open(UDAT_PATTERN, UDAT_PATTERN, IcuLocale(locale.get()),
                timeZoneChars.begin().get(), timeZoneChars.length(),
                patternChars.begin().get(), patternChars.length(), &status);
  if (U_FAILURE(status)) {
    intl::ReportInternalError(cx);
    return nullptr;
  }

  // ECMAScript requires the Gregorian calendar to be used from the beginning
  // of ECMAScript time.
  UCalendar* cal = const_cast<UCalendar*>(udat_getCalendar(df));
  ucal_setGregorianChange(cal, StartOfTime, &status);

  // An error here means the calendar is not Gregorian, so we don't care.

  return df;
}

static bool intl_FormatDateTime(JSContext* cx, const UDateFormat* df,
                                ClippedTime x, MutableHandleValue result) {
  MOZ_ASSERT(x.isValid());

  JSString* str =
      CallICU(cx, [df, x](UChar* chars, int32_t size, UErrorCode* status) {
        return udat_format(df, x.toDouble(), chars, size, nullptr, status);
      });
  if (!str) {
    return false;
  }

  result.setString(str);
  return true;
}

using FieldType = js::ImmutablePropertyNamePtr JSAtomState::*;

static FieldType GetFieldTypeForFormatField(UDateFormatField fieldName) {
  // See intl/icu/source/i18n/unicode/udat.h for a detailed field list.  This
  // switch is deliberately exhaustive: cases might have to be added/removed
  // if this code is compiled with a different ICU with more
  // UDateFormatField enum initializers.  Please guard such cases with
  // appropriate ICU version-testing #ifdefs, should cross-version divergence
  // occur.
  switch (fieldName) {
    case UDAT_ERA_FIELD:
      return &JSAtomState::era;

    case UDAT_YEAR_FIELD:
    case UDAT_YEAR_WOY_FIELD:
    case UDAT_EXTENDED_YEAR_FIELD:
      return &JSAtomState::year;

    case UDAT_YEAR_NAME_FIELD:
      return &JSAtomState::yearName;

    case UDAT_MONTH_FIELD:
    case UDAT_STANDALONE_MONTH_FIELD:
      return &JSAtomState::month;

    case UDAT_DATE_FIELD:
    case UDAT_JULIAN_DAY_FIELD:
      return &JSAtomState::day;

    case UDAT_HOUR_OF_DAY1_FIELD:
    case UDAT_HOUR_OF_DAY0_FIELD:
    case UDAT_HOUR1_FIELD:
    case UDAT_HOUR0_FIELD:
      return &JSAtomState::hour;

    case UDAT_MINUTE_FIELD:
      return &JSAtomState::minute;

    case UDAT_SECOND_FIELD:
      return &JSAtomState::second;

    case UDAT_DAY_OF_WEEK_FIELD:
    case UDAT_STANDALONE_DAY_FIELD:
    case UDAT_DOW_LOCAL_FIELD:
    case UDAT_DAY_OF_WEEK_IN_MONTH_FIELD:
      return &JSAtomState::weekday;

    case UDAT_AM_PM_FIELD:
      return &JSAtomState::dayPeriod;

    case UDAT_TIMEZONE_FIELD:
      return &JSAtomState::timeZoneName;

    case UDAT_FRACTIONAL_SECOND_FIELD:
      return &JSAtomState::fractionalSecond;

    case UDAT_FLEXIBLE_DAY_PERIOD_FIELD:
#ifdef NIGHTLY_BUILD
      return &JSAtomState::dayPeriod;
#else
      // Currently restricted to Nightly.
      return &JSAtomState::unknown;
#endif

#ifndef U_HIDE_INTERNAL_API
    case UDAT_RELATED_YEAR_FIELD:
      return &JSAtomState::relatedYear;
#endif

    case UDAT_DAY_OF_YEAR_FIELD:
    case UDAT_WEEK_OF_YEAR_FIELD:
    case UDAT_WEEK_OF_MONTH_FIELD:
    case UDAT_MILLISECONDS_IN_DAY_FIELD:
    case UDAT_TIMEZONE_RFC_FIELD:
    case UDAT_TIMEZONE_GENERIC_FIELD:
    case UDAT_QUARTER_FIELD:
    case UDAT_STANDALONE_QUARTER_FIELD:
    case UDAT_TIMEZONE_SPECIAL_FIELD:
    case UDAT_TIMEZONE_LOCALIZED_GMT_OFFSET_FIELD:
    case UDAT_TIMEZONE_ISO_FIELD:
    case UDAT_TIMEZONE_ISO_LOCAL_FIELD:
    case UDAT_AM_PM_MIDNIGHT_NOON_FIELD:
#ifndef U_HIDE_INTERNAL_API
    case UDAT_TIME_SEPARATOR_FIELD:
#endif
      // These fields are all unsupported.
      return &JSAtomState::unknown;

#ifndef U_HIDE_DEPRECATED_API
    case UDAT_FIELD_COUNT:
      MOZ_ASSERT_UNREACHABLE(
          "format field sentinel value returned by "
          "iterator!");
#endif
  }

  MOZ_ASSERT_UNREACHABLE(
      "unenumerated, undocumented format field returned "
      "by iterator");
  return nullptr;
}

static bool intl_FormatToPartsDateTime(JSContext* cx, const UDateFormat* df,
                                       ClippedTime x, FieldType source,
                                       MutableHandleValue result) {
  MOZ_ASSERT(x.isValid());

  UErrorCode status = U_ZERO_ERROR;
  UFieldPositionIterator* fpositer = ufieldpositer_open(&status);
  if (U_FAILURE(status)) {
    intl::ReportInternalError(cx);
    return false;
  }
  ScopedICUObject<UFieldPositionIterator, ufieldpositer_close> toClose(
      fpositer);

  RootedString overallResult(cx);
  overallResult = CallICU(
      cx, [df, x, fpositer](UChar* chars, int32_t size, UErrorCode* status) {
        return udat_formatForFields(df, x.toDouble(), chars, size, fpositer,
                                    status);
      });
  if (!overallResult) {
    return false;
  }

  RootedArrayObject partsArray(cx, NewDenseEmptyArray(cx));
  if (!partsArray) {
    return false;
  }

  if (overallResult->length() == 0) {
    // An empty string contains no parts, so avoid extra work below.
    result.setObject(*partsArray);
    return true;
  }

  size_t lastEndIndex = 0;

  RootedObject singlePart(cx);
  RootedValue val(cx);

  auto AppendPart = [&](FieldType type, size_t beginIndex, size_t endIndex) {
    singlePart = NewBuiltinClassInstance<PlainObject>(cx);
    if (!singlePart) {
      return false;
    }

    val = StringValue(cx->names().*type);
    if (!DefineDataProperty(cx, singlePart, cx->names().type, val)) {
      return false;
    }

    JSLinearString* partSubstr = NewDependentString(
        cx, overallResult, beginIndex, endIndex - beginIndex);
    if (!partSubstr) {
      return false;
    }

    val = StringValue(partSubstr);
    if (!DefineDataProperty(cx, singlePart, cx->names().value, val)) {
      return false;
    }

    if (source) {
      val = StringValue(cx->names().*source);
      if (!DefineDataProperty(cx, singlePart, cx->names().source, val)) {
        return false;
      }
    }

    if (!NewbornArrayPush(cx, partsArray, ObjectValue(*singlePart))) {
      return false;
    }

    lastEndIndex = endIndex;
    return true;
  };

  int32_t fieldInt, beginIndexInt, endIndexInt;
  while ((fieldInt = ufieldpositer_next(fpositer, &beginIndexInt,
                                        &endIndexInt)) >= 0) {
    MOZ_ASSERT(beginIndexInt >= 0);
    MOZ_ASSERT(endIndexInt >= 0);
    MOZ_ASSERT(beginIndexInt <= endIndexInt,
               "field iterator returning invalid range");

    size_t beginIndex(beginIndexInt);
    size_t endIndex(endIndexInt);

    // Technically this isn't guaranteed.  But it appears true in pratice,
    // and http://bugs.icu-project.org/trac/ticket/12024 is expected to
    // correct the documentation lapse.
    MOZ_ASSERT(lastEndIndex <= beginIndex,
               "field iteration didn't return fields in order start to "
               "finish as expected");

    if (FieldType type = GetFieldTypeForFormatField(
            static_cast<UDateFormatField>(fieldInt))) {
      if (lastEndIndex < beginIndex) {
        if (!AppendPart(&JSAtomState::literal, lastEndIndex, beginIndex)) {
          return false;
        }
      }

      if (!AppendPart(type, beginIndex, endIndex)) {
        return false;
      }
    }
  }

  // Append any final literal.
  if (lastEndIndex < overallResult->length()) {
    if (!AppendPart(&JSAtomState::literal, lastEndIndex,
                    overallResult->length())) {
      return false;
    }
  }

  result.setObject(*partsArray);
  return true;
}

bool js::intl_FormatDateTime(JSContext* cx, unsigned argc, Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);
  MOZ_ASSERT(args.length() == 3);
  MOZ_ASSERT(args[0].isObject());
  MOZ_ASSERT(args[1].isNumber());
  MOZ_ASSERT(args[2].isBoolean());

  Rooted<DateTimeFormatObject*> dateTimeFormat(cx);
  dateTimeFormat = &args[0].toObject().as<DateTimeFormatObject>();

  bool formatToParts = args[2].toBoolean();

  ClippedTime x = TimeClip(args[1].toNumber());
  if (!x.isValid()) {
    JS_ReportErrorNumberASCII(cx, GetErrorMessage, nullptr,
                              JSMSG_DATE_NOT_FINITE, "DateTimeFormat",
                              formatToParts ? "formatToParts" : "format");
    return false;
  }

  // Obtain a cached UDateFormat object.
  UDateFormat* df = dateTimeFormat->getDateFormat();
  if (!df) {
    df = NewUDateFormat(cx, dateTimeFormat);
    if (!df) {
      return false;
    }
    dateTimeFormat->setDateFormat(df);

    intl::AddICUCellMemory(dateTimeFormat,
                           DateTimeFormatObject::UDateFormatEstimatedMemoryUse);
  }

  // Use the UDateFormat to actually format the time stamp.
  FieldType source = nullptr;
  return formatToParts
             ? intl_FormatToPartsDateTime(cx, df, x, source, args.rval())
             : intl_FormatDateTime(cx, df, x, args.rval());
}

#ifndef U_HIDE_DRAFT_API
/**
 * Returns a new UDateIntervalFormat with the locale and date-time formatting
 * options of the given DateTimeFormat.
 */
static UDateIntervalFormat* NewUDateIntervalFormat(
    JSContext* cx, Handle<DateTimeFormatObject*> dateTimeFormat) {
  RootedValue value(cx);

  RootedObject internals(cx, intl::GetInternalsObject(cx, dateTimeFormat));
  if (!internals) {
    return nullptr;
  }

  if (!GetProperty(cx, internals, internals, cx->names().pattern, &value)) {
    return nullptr;
  }

  // Determine the hour cycle used in the resolved pattern. This is needed to
  // workaround <https://unicode-org.atlassian.net/browse/ICU-21154> and
  // <https://unicode-org.atlassian.net/browse/ICU-21155>.
  mozilla::Maybe<HourCycle> hcPattern;
  {
    JSLinearString* pattern = value.toString()->ensureLinear(cx);
    if (!pattern) {
      return nullptr;
    }

    JS::AutoCheckCannotGC nogc;
    if (pattern->hasLatin1Chars()) {
      hcPattern = HourCycleFromPattern<Latin1Char>(pattern->latin1Range(nogc));
    } else {
      hcPattern = HourCycleFromPattern<char16_t>(pattern->twoByteRange(nogc));
    }
  }

  UniqueChars locale = DateTimeFormatLocale(cx, internals, hcPattern);
  if (!locale) {
    return nullptr;
  }

  if (!GetProperty(cx, internals, internals, cx->names().timeZone, &value)) {
    return nullptr;
  }

  AutoStableStringChars timeZone(cx);
  if (!timeZone.initTwoByte(cx, value.toString())) {
    return nullptr;
  }
  mozilla::Span<const char16_t> timeZoneChars = timeZone.twoByteRange();

  if (!GetProperty(cx, internals, internals, cx->names().skeleton, &value)) {
    return nullptr;
  }

  AutoStableStringChars skeleton(cx);
  if (!skeleton.initTwoByte(cx, value.toString())) {
    return nullptr;
  }
  mozilla::Span<const char16_t> skeletonChars = skeleton.twoByteRange();

  Vector<char16_t, INITIAL_CHAR_BUFFER_SIZE> newSkeleton(cx);
  if (hcPattern) {
    if (!newSkeleton.append(skeletonChars.data(), skeletonChars.size())) {
      return nullptr;
    }

    ReplaceHourSymbol(newSkeleton, *hcPattern);
    skeletonChars = newSkeleton;
  }

  UErrorCode status = U_ZERO_ERROR;
  UDateIntervalFormat* dif = udtitvfmt_open(
      IcuLocale(locale.get()), skeletonChars.data(), skeletonChars.size(),
      timeZoneChars.data(), timeZoneChars.size(), &status);
  if (U_FAILURE(status)) {
    intl::ReportInternalError(cx);
    return nullptr;
  }

  return dif;
}

static UCalendar* CreateCalendar(JSContext* cx, const UCalendar* cal,
                                 ClippedTime t) {
  UErrorCode status = U_ZERO_ERROR;
  UCalendar* clone = ucal_clone(cal, &status);
  if (U_FAILURE(status)) {
    intl::ReportInternalError(cx);
    return nullptr;
  }
  ScopedICUObject<UCalendar, ucal_close> toClose(clone);

  ucal_setMillis(clone, t.toDouble(), &status);
  if (U_FAILURE(status)) {
    intl::ReportInternalError(cx);
    return nullptr;
  }
  return toClose.forget();
}

/**
 * PartitionDateTimeRangePattern ( dateTimeFormat, x, y )
 */
static const UFormattedValue* PartitionDateTimeRangePattern(
    JSContext* cx, const UDateFormat* df, const UDateIntervalFormat* dif,
    UFormattedDateInterval* formatted, ClippedTime x, ClippedTime y) {
  MOZ_ASSERT(x.isValid());
  MOZ_ASSERT(y.isValid());
  MOZ_ASSERT(x.toDouble() <= y.toDouble());

  // We can't access the calendar used by UDateIntervalFormat to change it to a
  // proleptic Gregorian calendar. Instead we need to call a different formatter
  // function which accepts UCalendar instead of UDate.
  // But creating new UCalendar objects for each call is slow, so when we can
  // ensure that the input dates are later than the Gregorian change date,
  // directly call the formatter functions taking UDate.

  // The Gregorian change date "1582-10-15T00:00:00.000Z".
  constexpr double GregorianChangeDate = -12219292800000.0;

  // Add a full day to account for time zone offsets.
  constexpr double GregorianChangeDatePlusOneDay =
      GregorianChangeDate + msPerDay;

  UErrorCode status = U_ZERO_ERROR;
  if (x.toDouble() < GregorianChangeDatePlusOneDay) {
    // Create calendar objects for the start and end date by cloning the date
    // formatter calendar. The date formatter calendar already has the correct
    // time zone set and was changed to use a proleptic Gregorian calendar.
    const UCalendar* cal = udat_getCalendar(df);

    UCalendar* startCal = CreateCalendar(cx, cal, x);
    if (!startCal) {
      return nullptr;
    }
    ScopedICUObject<UCalendar, ucal_close> toCloseStart(startCal);

    UCalendar* endCal = CreateCalendar(cx, cal, y);
    if (!endCal) {
      return nullptr;
    }
    ScopedICUObject<UCalendar, ucal_close> toCloseEnd(endCal);

    udtitvfmt_formatCalendarToResult(dif, startCal, endCal, formatted, &status);
  } else {
    // The common fast path which doesn't require creating calendar objects.
    udtitvfmt_formatToResult(dif, x.toDouble(), y.toDouble(), formatted,
                             &status);
  }
  if (U_FAILURE(status)) {
    intl::ReportInternalError(cx);
    return nullptr;
  }

  const UFormattedValue* formattedValue =
      udtitvfmt_resultAsValue(formatted, &status);
  if (U_FAILURE(status)) {
    intl::ReportInternalError(cx);
    return nullptr;
  }

  return formattedValue;
}

/**
 * PartitionDateTimeRangePattern ( dateTimeFormat, x, y ), steps 9-11.
 *
 * Examine the formatted value to see if any interval span field is present.
 */
static bool DateFieldsPracticallyEqual(JSContext* cx,
                                       const UFormattedValue* formattedValue,
                                       bool* equal) {
  UErrorCode status = U_ZERO_ERROR;
  UConstrainedFieldPosition* fpos = ucfpos_open(&status);
  if (U_FAILURE(status)) {
    intl::ReportInternalError(cx);
    return false;
  }
  ScopedICUObject<UConstrainedFieldPosition, ucfpos_close> toCloseFpos(fpos);

  // We're only interested in UFIELD_CATEGORY_DATE_INTERVAL_SPAN fields.
  ucfpos_constrainCategory(fpos, UFIELD_CATEGORY_DATE_INTERVAL_SPAN, &status);
  if (U_FAILURE(status)) {
    intl::ReportInternalError(cx);
    return false;
  }

  bool hasSpan = ufmtval_nextPosition(formattedValue, fpos, &status);
  if (U_FAILURE(status)) {
    intl::ReportInternalError(cx);
    return false;
  }

  // When no date interval span field was found, both dates are "practically
  // equal" per PartitionDateTimeRangePattern.
  *equal = !hasSpan;
  return true;
}

/**
 * FormatDateTimeRange( dateTimeFormat, x, y )
 */
static bool FormatDateTimeRange(JSContext* cx, const UDateFormat* df,
                                const UDateIntervalFormat* dif, ClippedTime x,
                                ClippedTime y, MutableHandleValue result) {
  UErrorCode status = U_ZERO_ERROR;
  UFormattedDateInterval* formatted = udtitvfmt_openResult(&status);
  if (U_FAILURE(status)) {
    intl::ReportInternalError(cx);
    return false;
  }
  ScopedICUObject<UFormattedDateInterval, udtitvfmt_closeResult> toClose(
      formatted);

  const UFormattedValue* formattedValue =
      PartitionDateTimeRangePattern(cx, df, dif, formatted, x, y);
  if (!formattedValue) {
    return false;
  }

  // PartitionDateTimeRangePattern, steps 9-11.
  bool equal;
  if (!DateFieldsPracticallyEqual(cx, formattedValue, &equal)) {
    return false;
  }

  // PartitionDateTimeRangePattern, step 12.
  if (equal) {
    return intl_FormatDateTime(cx, df, x, result);
  }

  JSString* resultStr = intl::FormattedValueToString(cx, formattedValue);
  if (!resultStr) {
    return false;
  }

  result.setString(resultStr);
  return true;
}

/**
 * FormatDateTimeRangeToParts ( dateTimeFormat, x, y )
 */
static bool FormatDateTimeRangeToParts(JSContext* cx, const UDateFormat* df,
                                       const UDateIntervalFormat* dif,
                                       ClippedTime x, ClippedTime y,
                                       MutableHandleValue result) {
  UErrorCode status = U_ZERO_ERROR;
  UFormattedDateInterval* formatted = udtitvfmt_openResult(&status);
  if (U_FAILURE(status)) {
    intl::ReportInternalError(cx);
    return false;
  }
  ScopedICUObject<UFormattedDateInterval, udtitvfmt_closeResult> toClose(
      formatted);

  const UFormattedValue* formattedValue =
      PartitionDateTimeRangePattern(cx, df, dif, formatted, x, y);
  if (!formattedValue) {
    return false;
  }

  // PartitionDateTimeRangePattern, steps 9-11.
  bool equal;
  if (!DateFieldsPracticallyEqual(cx, formattedValue, &equal)) {
    return false;
  }

  // PartitionDateTimeRangePattern, step 12.
  if (equal) {
    FieldType source = &JSAtomState::shared;
    return intl_FormatToPartsDateTime(cx, df, x, source, result);
  }

  RootedString overallResult(cx,
                             intl::FormattedValueToString(cx, formattedValue));
  if (!overallResult) {
    return false;
  }

  RootedArrayObject partsArray(cx, NewDenseEmptyArray(cx));
  if (!partsArray) {
    return false;
  }

  size_t lastEndIndex = 0;
  RootedObject singlePart(cx);
  RootedValue val(cx);

  auto AppendPart = [&](FieldType type, size_t beginIndex, size_t endIndex,
                        FieldType source) {
    singlePart = NewBuiltinClassInstance<PlainObject>(cx);
    if (!singlePart) {
      return false;
    }

    val = StringValue(cx->names().*type);
    if (!DefineDataProperty(cx, singlePart, cx->names().type, val)) {
      return false;
    }

    JSLinearString* partSubstr = NewDependentString(
        cx, overallResult, beginIndex, endIndex - beginIndex);
    if (!partSubstr) {
      return false;
    }

    val = StringValue(partSubstr);
    if (!DefineDataProperty(cx, singlePart, cx->names().value, val)) {
      return false;
    }

    val = StringValue(cx->names().*source);
    if (!DefineDataProperty(cx, singlePart, cx->names().source, val)) {
      return false;
    }

    if (!NewbornArrayPush(cx, partsArray, ObjectValue(*singlePart))) {
      return false;
    }

    lastEndIndex = endIndex;
    return true;
  };

  UConstrainedFieldPosition* fpos = ucfpos_open(&status);
  if (U_FAILURE(status)) {
    intl::ReportInternalError(cx);
    return false;
  }
  ScopedICUObject<UConstrainedFieldPosition, ucfpos_close> toCloseFpos(fpos);

  size_t categoryEndIndex = 0;
  FieldType source = &JSAtomState::shared;

  while (true) {
    bool hasMore = ufmtval_nextPosition(formattedValue, fpos, &status);
    if (U_FAILURE(status)) {
      intl::ReportInternalError(cx);
      return false;
    }
    if (!hasMore) {
      break;
    }

    int32_t category = ucfpos_getCategory(fpos, &status);
    if (U_FAILURE(status)) {
      intl::ReportInternalError(cx);
      return false;
    }

    int32_t field = ucfpos_getField(fpos, &status);
    if (U_FAILURE(status)) {
      intl::ReportInternalError(cx);
      return false;
    }

    int32_t beginIndexInt, endIndexInt;
    ucfpos_getIndexes(fpos, &beginIndexInt, &endIndexInt, &status);
    if (U_FAILURE(status)) {
      intl::ReportInternalError(cx);
      return false;
    }

    MOZ_ASSERT(beginIndexInt >= 0);
    MOZ_ASSERT(endIndexInt >= 0);
    MOZ_ASSERT(beginIndexInt <= endIndexInt,
               "field iterator returning invalid range");

    size_t beginIndex = size_t(beginIndexInt);
    size_t endIndex = size_t(endIndexInt);

    // Indices are guaranteed to be returned in order (from left to right).
    MOZ_ASSERT(lastEndIndex <= beginIndex,
               "field iteration didn't return fields in order start to "
               "finish as expected");

    if (category == UFIELD_CATEGORY_DATE_INTERVAL_SPAN) {
      // Append any remaining literal parts before changing the source kind.
      if (lastEndIndex < beginIndex) {
        if (!AppendPart(&JSAtomState::literal, lastEndIndex, beginIndex,
                        source)) {
          return false;
        }
      }

      // The special field category UFIELD_CATEGORY_DATE_INTERVAL_SPAN has only
      // two allowed values (0 or 1), indicating the begin of the start- resp.
      // end-date.
      MOZ_ASSERT(field == 0 || field == 1,
                 "span category has unexpected value");

      source = field == 0 ? &JSAtomState::startRange : &JSAtomState::endRange;
      categoryEndIndex = endIndex;
      continue;
    }

    // Ignore categories other than UFIELD_CATEGORY_DATE.
    if (category != UFIELD_CATEGORY_DATE) {
      continue;
    }

    // Append the field if supported. If not supported, append it as part of the
    // next literal part.
    if (FieldType type =
            GetFieldTypeForFormatField(static_cast<UDateFormatField>(field))) {
      if (lastEndIndex < beginIndex) {
        if (!AppendPart(&JSAtomState::literal, lastEndIndex, beginIndex,
                        source)) {
          return false;
        }
      }

      if (!AppendPart(type, beginIndex, endIndex, source)) {
        return false;
      }
    }

    if (endIndex == categoryEndIndex) {
      // Append any remaining literal parts before changing the source kind.
      if (lastEndIndex < endIndex) {
        if (!AppendPart(&JSAtomState::literal, lastEndIndex, endIndex,
                        source)) {
          return false;
        }
      }

      source = &JSAtomState::shared;
    }
  }

  // Append any final literal.
  if (lastEndIndex < overallResult->length()) {
    if (!AppendPart(&JSAtomState::literal, lastEndIndex,
                    overallResult->length(), source)) {
      return false;
    }
  }

  result.setObject(*partsArray);
  return true;
}

bool js::intl_FormatDateTimeRange(JSContext* cx, unsigned argc, Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);
  MOZ_ASSERT(args.length() == 4);
  MOZ_ASSERT(args[0].isObject());
  MOZ_ASSERT(args[1].isNumber());
  MOZ_ASSERT(args[2].isNumber());
  MOZ_ASSERT(args[3].isBoolean());

  Rooted<DateTimeFormatObject*> dateTimeFormat(cx);
  dateTimeFormat = &args[0].toObject().as<DateTimeFormatObject>();

  bool formatToParts = args[3].toBoolean();

  // PartitionDateTimeRangePattern, steps 1-2.
  ClippedTime x = TimeClip(args[1].toNumber());
  if (!x.isValid()) {
    JS_ReportErrorNumberASCII(
        cx, GetErrorMessage, nullptr, JSMSG_DATE_NOT_FINITE, "DateTimeFormat",
        formatToParts ? "formatRangeToParts" : "formatRange");
    return false;
  }

  // PartitionDateTimeRangePattern, steps 3-4.
  ClippedTime y = TimeClip(args[2].toNumber());
  if (!y.isValid()) {
    JS_ReportErrorNumberASCII(
        cx, GetErrorMessage, nullptr, JSMSG_DATE_NOT_FINITE, "DateTimeFormat",
        formatToParts ? "formatRangeToParts" : "formatRange");
    return false;
  }

  // Self-hosted code should have checked this condition.
  MOZ_ASSERT(x.toDouble() <= y.toDouble(),
             "start date mustn't be after the end date");

  // Obtain a cached UDateFormat object.
  UDateFormat* df = dateTimeFormat->getDateFormat();
  if (!df) {
    df = NewUDateFormat(cx, dateTimeFormat);
    if (!df) {
      return false;
    }
    dateTimeFormat->setDateFormat(df);

    intl::AddICUCellMemory(dateTimeFormat,
                           DateTimeFormatObject::UDateFormatEstimatedMemoryUse);
  }

  // Obtain a cached UDateIntervalFormat object.
  UDateIntervalFormat* dif = dateTimeFormat->getDateIntervalFormat();
  if (!dif) {
    dif = NewUDateIntervalFormat(cx, dateTimeFormat);
    if (!dif) {
      return false;
    }
    dateTimeFormat->setDateIntervalFormat(dif);

    intl::AddICUCellMemory(
        dateTimeFormat,
        DateTimeFormatObject::UDateIntervalFormatEstimatedMemoryUse);
  }

  // Use the UDateIntervalFormat to actually format the time range.
  return formatToParts
             ? FormatDateTimeRangeToParts(cx, df, dif, x, y, args.rval())
             : FormatDateTimeRange(cx, df, dif, x, y, args.rval());
}

#else
bool js::intl_FormatDateTimeRange(JSContext* cx, unsigned argc, Value* vp) {
  MOZ_CRASH("FormatDateTimeRange requires ICU draft APIs");
}
#endif  // U_HIDE_DRAFT_API
