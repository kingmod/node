// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_INTL_SUPPORT
#error Internationalization is expected to be enabled.
#endif  // V8_INTL_SUPPORT

#include <cmath>
#include <memory>

#include "src/api-inl.h"
#include "src/api-natives.h"
#include "src/arguments-inl.h"
#include "src/date.h"
#include "src/global-handles.h"
#include "src/heap/factory.h"
#include "src/intl.h"
#include "src/isolate-inl.h"
#include "src/messages.h"
#include "src/objects/intl-objects-inl.h"
#include "src/objects/intl-objects.h"
#include "src/objects/js-array-inl.h"
#include "src/objects/js-collator-inl.h"
#include "src/objects/js-date-time-format-inl.h"
#include "src/objects/js-list-format-inl.h"
#include "src/objects/js-list-format.h"
#include "src/objects/js-number-format-inl.h"
#include "src/objects/js-plural-rules-inl.h"
#include "src/objects/managed.h"
#include "src/runtime/runtime-utils.h"
#include "src/utils.h"

#include "unicode/brkiter.h"
#include "unicode/calendar.h"
#include "unicode/coll.h"
#include "unicode/curramt.h"
#include "unicode/datefmt.h"
#include "unicode/dcfmtsym.h"
#include "unicode/decimfmt.h"
#include "unicode/dtfmtsym.h"
#include "unicode/dtptngen.h"
#include "unicode/locid.h"
#include "unicode/numfmt.h"
#include "unicode/numsys.h"
#include "unicode/plurrule.h"
#include "unicode/smpdtfmt.h"
#include "unicode/timezone.h"
#include "unicode/uchar.h"
#include "unicode/ucol.h"
#include "unicode/ucurr.h"
#include "unicode/uloc.h"
#include "unicode/unistr.h"
#include "unicode/unum.h"
#include "unicode/uversion.h"


namespace v8 {
namespace internal {

// ecma402 #sec-formatlist
RUNTIME_FUNCTION(Runtime_FormatList) {
  HandleScope scope(isolate);
  DCHECK_EQ(2, args.length());
  CONVERT_ARG_HANDLE_CHECKED(JSListFormat, list_format, 0);
  CONVERT_ARG_HANDLE_CHECKED(JSArray, list, 1);
  RETURN_RESULT_OR_FAILURE(
      isolate, JSListFormat::FormatList(isolate, list_format, list));
}

// ecma402 #sec-formatlisttoparts
RUNTIME_FUNCTION(Runtime_FormatListToParts) {
  HandleScope scope(isolate);
  DCHECK_EQ(2, args.length());
  CONVERT_ARG_HANDLE_CHECKED(JSListFormat, list_format, 0);
  CONVERT_ARG_HANDLE_CHECKED(JSArray, list, 1);
  RETURN_RESULT_OR_FAILURE(
      isolate, JSListFormat::FormatListToParts(isolate, list_format, list));
}

// ECMA 402 6.2.3
RUNTIME_FUNCTION(Runtime_CanonicalizeLanguageTag) {
  HandleScope scope(isolate);

  DCHECK_EQ(1, args.length());
  CONVERT_ARG_HANDLE_CHECKED(Object, locale, 0);

  std::string canonicalized;
  if (!Intl::CanonicalizeLanguageTag(isolate, locale).To(&canonicalized)) {
    return ReadOnlyRoots(isolate).exception();
  }
  return *isolate->factory()->NewStringFromAsciiChecked(canonicalized.c_str());
}

RUNTIME_FUNCTION(Runtime_AvailableLocalesOf) {
  HandleScope scope(isolate);
  DCHECK_EQ(1, args.length());
  CONVERT_ARG_HANDLE_CHECKED(String, service, 0);
  Handle<JSObject> locales;
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
      isolate, locales, Intl::AvailableLocalesOf(isolate, service));
  return *locales;
}

RUNTIME_FUNCTION(Runtime_GetDefaultICULocale) {
  HandleScope scope(isolate);

  DCHECK_EQ(0, args.length());
  return *isolate->factory()->NewStringFromAsciiChecked(
      Intl::DefaultLocale(isolate).c_str());
}

RUNTIME_FUNCTION(Runtime_DefineWEProperty) {
  HandleScope scope(isolate);

  DCHECK_EQ(3, args.length());
  CONVERT_ARG_HANDLE_CHECKED(JSObject, target, 0);
  CONVERT_ARG_HANDLE_CHECKED(Name, key, 1);
  CONVERT_ARG_HANDLE_CHECKED(Object, value, 2);
  Intl::DefineWEProperty(isolate, target, key, value);
  return ReadOnlyRoots(isolate).undefined_value();
}

RUNTIME_FUNCTION(Runtime_IsInitializedIntlObjectOfType) {
  HandleScope scope(isolate);

  DCHECK_EQ(2, args.length());

  CONVERT_ARG_HANDLE_CHECKED(Object, input, 0);
  CONVERT_SMI_ARG_CHECKED(expected_type_int, 1);

  Intl::Type expected_type = Intl::TypeFromInt(expected_type_int);

  return isolate->heap()->ToBoolean(
      Intl::IsObjectOfType(isolate, input, expected_type));
}

RUNTIME_FUNCTION(Runtime_MarkAsInitializedIntlObjectOfType) {
  HandleScope scope(isolate);

  DCHECK_EQ(2, args.length());

  CONVERT_ARG_HANDLE_CHECKED(JSObject, input, 0);
  CONVERT_ARG_HANDLE_CHECKED(Smi, type, 1);

#ifdef DEBUG
  // TypeFromSmi does correctness checks.
  Intl::Type type_intl = Intl::TypeFromSmi(*type);
  USE(type_intl);
#endif

  Handle<Symbol> marker = isolate->factory()->intl_initialized_marker_symbol();
  JSObject::SetProperty(isolate, input, marker, type, LanguageMode::kStrict)
      .Assert();

  return ReadOnlyRoots(isolate).undefined_value();
}

RUNTIME_FUNCTION(Runtime_CreateDateTimeFormat) {
  HandleScope scope(isolate);

  DCHECK_EQ(3, args.length());

  CONVERT_ARG_HANDLE_CHECKED(String, locale, 0);
  CONVERT_ARG_HANDLE_CHECKED(JSObject, options, 1);
  CONVERT_ARG_HANDLE_CHECKED(JSObject, resolved, 2);

  Handle<JSFunction> constructor(
      isolate->native_context()->intl_date_time_format_function(), isolate);

  Handle<JSObject> local_object;
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(isolate, local_object,
                                     JSObject::New(constructor, constructor));

  // Set date time formatter as embedder field of the resulting JS object.
  Maybe<icu::SimpleDateFormat*> maybe_date_format =
      DateFormat::InitializeDateTimeFormat(isolate, locale, options, resolved);
  MAYBE_RETURN(maybe_date_format, ReadOnlyRoots(isolate).exception());
  icu::SimpleDateFormat* date_format = maybe_date_format.FromJust();
  CHECK_NOT_NULL(date_format);

  local_object->SetEmbedderField(DateFormat::kSimpleDateFormatIndex,
                                 reinterpret_cast<Smi*>(date_format));

  // Make object handle weak so we can delete the data format once GC kicks in.
  Handle<Object> wrapper = isolate->global_handles()->Create(*local_object);
  GlobalHandles::MakeWeak(wrapper.location(), wrapper.location(),
                          DateFormat::DeleteDateFormat,
                          WeakCallbackType::kInternalFields);
  return *local_object;
}

// ecma402/#sec-intl.datetimeformat.prototype.resolvedoptions
RUNTIME_FUNCTION(Runtime_DateTimeFormatResolvedOptions) {
  HandleScope scope(isolate);
  DCHECK_EQ(1, args.length());
  // 1. Let dtf be this value.
  CONVERT_ARG_HANDLE_CHECKED(Object, dtf, 0);
  // 2. If Type(dtf) is not Object, throw a TypeError exception.
  if (!dtf->IsJSReceiver()) {
    Handle<String> method_str = isolate->factory()->NewStringFromStaticChars(
        "Intl.DateTimeFormat.prototype.resolvedOptions");
    THROW_NEW_ERROR_RETURN_FAILURE(
        isolate, NewTypeError(MessageTemplate::kIncompatibleMethodReceiver,
                              method_str, dtf));
  }
  Handle<JSReceiver> date_format_holder = Handle<JSReceiver>::cast(dtf);
  RETURN_RESULT_OR_FAILURE(
      isolate, JSDateTimeFormat::ResolvedOptions(isolate, date_format_holder));
}

RUNTIME_FUNCTION(Runtime_NumberFormatResolvedOptions) {
  HandleScope scope(isolate);

  DCHECK_EQ(1, args.length());
  CONVERT_ARG_HANDLE_CHECKED(Object, number_format_obj, 0);

  // 2. If Type(nf) is not Object, throw a TypeError exception
  if (!number_format_obj->IsJSReceiver()) {
    Handle<String> method_str = isolate->factory()->NewStringFromStaticChars(
        "Intl.NumberFormat.prototype.resolvedOptions");
    THROW_NEW_ERROR_RETURN_FAILURE(
        isolate, NewTypeError(MessageTemplate::kIncompatibleMethodReceiver,
                              method_str, number_format_obj));
  }

  // 3. Let nf be ? UnwrapNumberFormat(nf).
  Handle<JSReceiver> format_holder =
      Handle<JSReceiver>::cast(number_format_obj);

  Handle<JSNumberFormat> number_format;
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
      isolate, number_format,
      JSNumberFormat::UnwrapNumberFormat(isolate, format_holder));

  return *JSNumberFormat::ResolvedOptions(isolate, number_format);
}

RUNTIME_FUNCTION(Runtime_CollatorResolvedOptions) {
  HandleScope scope(isolate);

  DCHECK_EQ(1, args.length());
  CONVERT_ARG_HANDLE_CHECKED(Object, collator_obj, 0);

  // 3. If pr does not have an [[InitializedCollator]] internal
  // slot, throw a TypeError exception.
  if (!collator_obj->IsJSCollator()) {
    Handle<String> method_str = isolate->factory()->NewStringFromStaticChars(
        "Intl.Collator.prototype.resolvedOptions");
    THROW_NEW_ERROR_RETURN_FAILURE(
        isolate, NewTypeError(MessageTemplate::kIncompatibleMethodReceiver,
                              method_str, collator_obj));
  }

  Handle<JSCollator> collator = Handle<JSCollator>::cast(collator_obj);

  return *JSCollator::ResolvedOptions(isolate, collator);
}

RUNTIME_FUNCTION(Runtime_ParseExtension) {
  Factory* factory = isolate->factory();
  HandleScope scope(isolate);
  DCHECK_EQ(1, args.length());
  CONVERT_ARG_HANDLE_CHECKED(String, extension, 0);
  std::map<std::string, std::string> map;
  Intl::ParseExtension(isolate, std::string(extension->ToCString().get()), map);
  Handle<JSObject> extension_map =
      isolate->factory()->NewJSObjectWithNullProto();
  for (std::map<std::string, std::string>::iterator it = map.begin();
       it != map.end(); it++) {
    JSObject::AddProperty(
        isolate, extension_map,
        factory->NewStringFromAsciiChecked(it->first.c_str()),
        factory->NewStringFromAsciiChecked(it->second.c_str()), NONE);
  }
  return *extension_map;
}

RUNTIME_FUNCTION(Runtime_PluralRulesSelect) {
  HandleScope scope(isolate);

  DCHECK_EQ(2, args.length());
  CONVERT_ARG_HANDLE_CHECKED(Object, plural_rules_obj, 0);
  CONVERT_ARG_HANDLE_CHECKED(Object, number, 1);

  // 3. If pr does not have an [[InitializedPluralRules]] internal
  // slot, throw a TypeError exception.
  if (!plural_rules_obj->IsJSPluralRules()) {
    Handle<String> method_str = isolate->factory()->NewStringFromStaticChars(
        "Intl.PluralRules.prototype.select");
    THROW_NEW_ERROR_RETURN_FAILURE(
        isolate, NewTypeError(MessageTemplate::kIncompatibleMethodReceiver,
                              method_str, plural_rules_obj));
  }

  Handle<JSPluralRules> plural_rules =
      Handle<JSPluralRules>::cast(plural_rules_obj);

  // 4. Return ? ResolvePlural(pr, n).

  RETURN_RESULT_OR_FAILURE(
      isolate, JSPluralRules::ResolvePlural(isolate, plural_rules, number));
}

RUNTIME_FUNCTION(Runtime_ToDateTimeOptions) {
  HandleScope scope(isolate);
  DCHECK_EQ(args.length(), 3);
  CONVERT_ARG_HANDLE_CHECKED(Object, options, 0);
  CONVERT_ARG_HANDLE_CHECKED(String, required, 1);
  CONVERT_ARG_HANDLE_CHECKED(String, defaults, 2);
  RETURN_RESULT_OR_FAILURE(isolate,
                           JSDateTimeFormat::ToDateTimeOptions(
                               isolate, options, required->ToCString().get(),
                               defaults->ToCString().get()));
}

RUNTIME_FUNCTION(Runtime_StringToLowerCaseIntl) {
  HandleScope scope(isolate);
  DCHECK_EQ(args.length(), 1);
  CONVERT_ARG_HANDLE_CHECKED(String, s, 0);
  s = String::Flatten(isolate, s);
  RETURN_RESULT_OR_FAILURE(isolate, ConvertToLower(s, isolate));
}

RUNTIME_FUNCTION(Runtime_StringToUpperCaseIntl) {
  HandleScope scope(isolate);
  DCHECK_EQ(args.length(), 1);
  CONVERT_ARG_HANDLE_CHECKED(String, s, 0);
  s = String::Flatten(isolate, s);
  RETURN_RESULT_OR_FAILURE(isolate, ConvertToUpper(s, isolate));
}

RUNTIME_FUNCTION(Runtime_DateCacheVersion) {
  HandleScope scope(isolate);
  DCHECK_EQ(0, args.length());
  if (isolate->serializer_enabled())
    return ReadOnlyRoots(isolate).undefined_value();
  if (!isolate->eternal_handles()->Exists(EternalHandles::DATE_CACHE_VERSION)) {
    Handle<FixedArray> date_cache_version =
        isolate->factory()->NewFixedArray(1, TENURED);
    date_cache_version->set(0, Smi::kZero);
    isolate->eternal_handles()->CreateSingleton(
        isolate, *date_cache_version, EternalHandles::DATE_CACHE_VERSION);
  }
  Handle<FixedArray> date_cache_version =
      Handle<FixedArray>::cast(isolate->eternal_handles()->GetSingleton(
          EternalHandles::DATE_CACHE_VERSION));
  return date_cache_version->get(0);
}

RUNTIME_FUNCTION(Runtime_IntlUnwrapReceiver) {
  HandleScope scope(isolate);
  DCHECK_EQ(5, args.length());
  CONVERT_ARG_HANDLE_CHECKED(JSReceiver, receiver, 0);
  CONVERT_SMI_ARG_CHECKED(type_int, 1);
  CONVERT_ARG_HANDLE_CHECKED(JSFunction, constructor, 2);
  CONVERT_ARG_HANDLE_CHECKED(String, method, 3);
  CONVERT_BOOLEAN_ARG_CHECKED(check_legacy_constructor, 4);

  RETURN_RESULT_OR_FAILURE(
      isolate, Intl::UnwrapReceiver(isolate, receiver, constructor,
                                    Intl::TypeFromInt(type_int), method,
                                    check_legacy_constructor));
}

}  // namespace internal
}  // namespace v8
