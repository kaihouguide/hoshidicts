import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath, pathToFileURL} from 'node:url';

const scriptDir = path.dirname(fileURLToPath(import.meta.url));
const repoRoot = path.resolve(scriptDir, '..');
const yomitanRoot = process.env.YOMITAN_DIR ? path.resolve(process.env.YOMITAN_DIR) : path.resolve(repoRoot, '..', 'yomitan');

async function importYomitan(relativePath) {
  return import(pathToFileURL(path.join(yomitanRoot, relativePath)).href);
}

const [
  {arabicTransforms},
  {germanTransforms},
  {modernGreekTransforms},
  {englishTransforms},
  {esperantoTransforms},
  {spanishTransforms},
  {basqueTransforms},
  {frenchTransforms},
  {irishTransforms},
  {ancientGreekTransforms},
  {japaneseTransforms},
  {georgianTransforms},
  {koreanTransforms},
  {latinTransforms},
  {oldIrishTransforms},
  {albanianTransforms},
  {tagalogTransforms},
  {yiddishTransforms},
] = await Promise.all([
  importYomitan('ext/js/language/ar/arabic-transforms.js'),
  importYomitan('ext/js/language/de/german-transforms.js'),
  importYomitan('ext/js/language/el/modern-greek-transforms.js'),
  importYomitan('ext/js/language/en/english-transforms.js'),
  importYomitan('ext/js/language/eo/esperanto-transforms.js'),
  importYomitan('ext/js/language/es/spanish-transforms.js'),
  importYomitan('ext/js/language/eu/basque-transforms.js'),
  importYomitan('ext/js/language/fr/french-transforms.js'),
  importYomitan('ext/js/language/ga/irish-transforms.js'),
  importYomitan('ext/js/language/grc/ancient-greek-transforms.js'),
  importYomitan('ext/js/language/ja/japanese-transforms.js'),
  importYomitan('ext/js/language/ka/georgian-transforms.js'),
  importYomitan('ext/js/language/ko/korean-transforms.js'),
  importYomitan('ext/js/language/la/latin-transforms.js'),
  importYomitan('ext/js/language/sga/old-irish-transforms.js'),
  importYomitan('ext/js/language/sq/albanian-transforms.js'),
  importYomitan('ext/js/language/tl/tagalog-transforms.js'),
  importYomitan('ext/js/language/yi/yiddish-transforms.js'),
]);

const descriptors = [
  arabicTransforms,
  germanTransforms,
  modernGreekTransforms,
  englishTransforms,
  esperantoTransforms,
  spanishTransforms,
  basqueTransforms,
  frenchTransforms,
  irishTransforms,
  ancientGreekTransforms,
  japaneseTransforms,
  georgianTransforms,
  koreanTransforms,
  latinTransforms,
  oldIrishTransforms,
  albanianTransforms,
  tagalogTransforms,
  yiddishTransforms,
];

const KINDS = {
  suffix: 'Suffix',
  prefix: 'Prefix',
  wholeWord: 'WholeWord',
  sandwich: 'Sandwich',
  asciiWordSandwich: 'AsciiWordSandwich',
  guardedPrefix: 'GuardedPrefix',
  guardedSuffix: 'GuardedSuffix',
  arabicSandwich: 'ArabicSandwich',
  guardedSuffixNotAfterJ: 'GuardedSuffixNotAfterJ',
  replaceAll: 'ReplaceAll',
  replaceAllNotAtStart: 'ReplaceAllNotAtStart',
  greekXiAnaNormalize: 'GreekXiAnaNormalize',
  englishPhrasalSuffix: 'EnglishPhrasalSuffix',
  englishInterposedPhrasalObject: 'EnglishInterposedPhrasalObject',
  spanishStemSuffix: 'SpanishStemSuffix',
  spanishJugarStemSuffix: 'SpanishJugarStemSuffix',
  spanishOlerStemSuffix: 'SpanishOlerStemSuffix',
  spanishPronominal: 'SpanishPronominal',
  germanSeparatedPrefix: 'GermanSeparatedPrefix',
  germanBasicPastParticiple: 'GermanBasicPastParticiple',
  germanSeparablePastParticiple: 'GermanSeparablePastParticiple',
  tagalogRemoveFirst: 'TagalogRemoveFirst',
  tagalogSuffixOToU: 'TagalogSuffixOToU',
  tagalogPrefixReduplication: 'TagalogPrefixReduplication',
  tagalogSandwichOToU: 'TagalogSandwichOToU',
  yiddishUmlautSuffix: 'YiddishUmlautSuffix',
};

const YIDDISH_MUTATIONS = [
  {new: '\u05e2', orig: '\ufb2e'},
  {new: '\u05e2', orig: '\ufb2f'},
  {new: '\u05e2', orig: '\u05d0'},
  {new: '\u05f1', orig: '\u05e2'},
  {new: '\u05f2', orig: '\u05f1'},
  {new: '\u05d9', orig: '\u05d5'},
];

function functionSource(rule) {
  return String(rule.deinflect).replace(/\s+/g, ' ').trim();
}

function hasOwn(object, key) {
  return Object.prototype.hasOwnProperty.call(object, key);
}

function regexLiteralToString(source) {
  let result = '';
  for (let i = 0; i < source.length; ++i) {
    const c = source[i];
    if (c !== '\\') {
      result += c;
      continue;
    }
    const next = source[++i];
    if (next === 'u') {
      if (source[i + 1] === '{') {
        const end = source.indexOf('}', i + 2);
        result += String.fromCodePoint(Number.parseInt(source.slice(i + 2, end), 16));
        i = end;
      } else {
        result += String.fromCharCode(Number.parseInt(source.slice(i + 1, i + 5), 16));
        i += 4;
      }
    } else if (next === 'x') {
      result += String.fromCharCode(Number.parseInt(source.slice(i + 1, i + 3), 16));
      i += 2;
    } else {
      result += next;
    }
  }
  return result;
}

function stripAnchors(source) {
  return source.replace(/^\^/, '').replace(/\$$/, '');
}

function literalPrefixFromRegex(rule) {
  return regexLiteralToString(rule.isInflected.source.replace(/^\^/, ''));
}

function literalWholeWordFromRegex(rule) {
  return regexLiteralToString(stripAnchors(rule.isInflected.source));
}

function inferPrefix(rule) {
  const left = literalPrefixFromRegex(rule);
  const marker = 'stem';
  const input = `${left}${marker}`;
  const output = rule.deinflect(input);
  for (let prefixLength = left.length; prefixLength >= 0; --prefixLength) {
    const guardedStem = `${left.slice(prefixLength)}${marker}`;
    if (output.endsWith(guardedStem)) {
      return {
        inflectedPrefix: left.slice(0, prefixLength),
        deinflectedPrefix: output.slice(0, output.length - guardedStem.length),
        initialStemSegment: left.slice(prefixLength),
      };
    }
  }
  throw new Error(`Could not infer prefix rule from ${rule.isInflected}`);
}

function inferSuffix(rule) {
  const source = rule.isInflected.source;
  if (source.startsWith('.*[^j]')) {
    const suffix = regexLiteralToString(source.slice('.*[^j]'.length, -1));
    return {kind: KINDS.guardedSuffixNotAfterJ, suffix, deinflectedSuffix: rule.deinflected};
  }
  if (source === '$') {
    return {kind: KINDS.suffix, suffix: '', deinflectedSuffix: rule.deinflected, finalStemSegment: ''};
  }

  const right = regexLiteralToString(source.replace(/\$$/, ''));
  const marker = 'stem';
  const input = `${marker}${right}`;
  const output = rule.deinflect(input);
  for (let suffixLength = right.length; suffixLength >= 0; --suffixLength) {
    const guardedStem = `${marker}${right.slice(0, right.length - suffixLength)}`;
    if (output.startsWith(guardedStem)) {
      const deinflectedSuffix = output.slice(guardedStem.length);
      if (!hasOwn(rule, 'deinflected') || deinflectedSuffix === rule.deinflected) {
        return {
          kind: right.length === suffixLength ? KINDS.suffix : KINDS.guardedSuffix,
          suffix: right.slice(right.length - suffixLength),
          deinflectedSuffix,
          finalStemSegment: right.slice(0, right.length - suffixLength),
        };
      }
    }
  }
  throw new Error(`Could not infer suffix rule from ${rule.isInflected}`);
}

function inferSandwich(rule, language) {
  const source = rule.isInflected.source;
  const wordToken = source.indexOf('\\w+');
  let leftSource;
  let rightSource;
  let marker;
  let kind;

  if (wordToken >= 0) {
    leftSource = source.slice(1, wordToken);
    rightSource = source.slice(wordToken + '\\w+'.length, -1);
    marker = 'root';
    kind = language === 'tl' ? KINDS.asciiWordSandwich : KINDS.sandwich;
  } else {
    const classStart = source.indexOf('[');
    const classEnd = source.indexOf(']+', classStart);
    if (classStart < 0 || classEnd < 0) {
      throw new Error(`Could not find sandwich middle in ${rule.isInflected}`);
    }
    leftSource = source.slice(1, classStart);
    rightSource = source.slice(classEnd + 2, -1);
    marker = 'ب';
    kind = language === 'ar' ? KINDS.arabicSandwich : KINDS.sandwich;
  }

  const left = regexLiteralToString(leftSource);
  const right = regexLiteralToString(rightSource);
  const input = `${left}${marker}${right}`;
  const output = rule.deinflect(input);

  for (let prefixLength = left.length; prefixLength >= 0; --prefixLength) {
    for (let suffixLength = right.length; suffixLength >= 0; --suffixLength) {
      const inner = `${left.slice(prefixLength)}${marker}${right.slice(0, right.length - suffixLength)}`;
      const index = output.indexOf(inner);
      if (index >= 0) {
        return {
          kind,
          inflectedPrefix: left.slice(0, prefixLength),
          deinflectedPrefix: output.slice(0, index),
          inflectedSuffix: right.slice(right.length - suffixLength),
          deinflectedSuffix: output.slice(index + inner.length),
          initialStemSegment: left.slice(prefixLength),
          finalStemSegment: right.slice(0, right.length - suffixLength),
        };
      }
    }
  }
  throw new Error(`Could not infer sandwich rule from ${rule.isInflected}`);
}

function parseSpanishSimpleStem(rule) {
  const source = functionSource(rule);
  const match = source.match(/term\.replace\(\/([^/]+)\/, '([^']*)'\)\.replace\(\/\(([^/]+)\)\$\/, '([^']*)'\)/);
  if (!match) {
    return null;
  }
  return {
    kind: KINDS.spanishStemSuffix,
    inflectedStem: match[1],
    deinflectedStem: match[2],
    suffixes: match[3],
    deinflectedSuffix: match[4],
  };
}

function parseSpanishJugarStem(rule) {
  const source = functionSource(rule);
  if (!source.includes("term.startsWith('jue')")) {
    return null;
  }
  const matches = [...source.matchAll(/replace\(\/\(([^/]+)\)\$\/, '([^']*)'\)/g)];
  if (matches.length < 2) {
    throw new Error(`Could not parse jugar stem callback: ${source}`);
  }
  return {
    kind: KINDS.spanishJugarStemSuffix,
    inflectedStem: 'ue',
    deinflectedStem: 'o',
    suffixes: matches[1][1],
    deinflectedSuffix: matches[1][2],
    specialSuffixes: matches[0][1],
  };
}

function parseSpanishOlerStem(rule) {
  const source = functionSource(rule);
  if (!source.includes("term.startsWith('hue')")) {
    return null;
  }
  const match = source.match(/replace\(\/\(([^/]+)\)\$\/, '([^']*)'\)/);
  if (!match) {
    throw new Error(`Could not parse oler stem callback: ${source}`);
  }
  return {
    kind: KINDS.spanishOlerStemSuffix,
    inflectedStem: 'ue',
    deinflectedStem: 'o',
    suffixes: match[1],
    deinflectedSuffix: match[2],
  };
}

function parseEnglishPhrasalSuffix(rule) {
  const match = rule.isInflected.source.match(/^\^\\w\*(.*?) \(\?:/);
  if (!match) {
    return null;
  }
  const inflectedSuffix = regexLiteralToString(match[1]);
  const probe = `word${inflectedSuffix} up`;
  const output = rule.deinflect(probe);
  const prefix = 'word';
  const suffix = ' up';
  if (!output.startsWith(prefix) || !output.endsWith(suffix)) {
    throw new Error(`Could not infer English phrasal suffix from ${rule.isInflected}`);
  }
  return {
    kind: KINDS.englishPhrasalSuffix,
    inflectedSuffix,
    deinflectedSuffix: output.slice(prefix.length, output.length - suffix.length),
  };
}

function parseGermanSeparablePastParticiple(rule) {
  const source = rule.isInflected.source;
  const match = source.match(/^\^\((.*)\)ge\(\[.*\]\+\)t\$$/);
  if (!match) {
    return null;
  }
  const suffix = rule.deinflect('abgearbeitet').slice('abarbeit'.length);
  return match[1].split('|').map((prefix) => ({
    kind: KINDS.germanSeparablePastParticiple,
    prefix: regexLiteralToString(prefix),
    suffix,
  }));
}

function parseTagalogPrefixReduplication(rule) {
  const match = rule.isInflected.source.match(/^\^\((.*)\)\(\[([^\]]*)\]\*\[aeiou\]\)\(\\2\)/);
  if (!match) {
    return null;
  }
  const prefix = regexLiteralToString(match[1]);
  const consonants = regexLiteralToString(match[2]);
  const syllable = 'ba';
  const output = rule.deinflect(`${prefix}${syllable}${syllable}rest`);
  if (!output.endsWith(`${syllable}rest`)) {
    throw new Error(`Could not infer Tagalog reduplication from ${rule.isInflected}`);
  }
  return {
    kind: KINDS.tagalogPrefixReduplication,
    prefix,
    deinflectedPrefix: output.slice(0, output.length - `${syllable}rest`.length),
    consonants,
  };
}

function parseTagalogSuffixOToU(rule) {
  const match = rule.isInflected.source.match(/^u\(\[[^\]]+\]\+\)(.*)\$$/);
  if (!match) {
    return null;
  }
  const suffix = regexLiteralToString(match[1]);
  const output = rule.deinflect(`puxt${suffix}`);
  const expectedStem = 'poxt';
  if (!output.startsWith(expectedStem)) {
    throw new Error(`Could not infer Tagalog O-to-U suffix from ${rule.isInflected}`);
  }
  return {
    kind: KINDS.tagalogSuffixOToU,
    suffix,
    deinflectedSuffix: output.slice(expectedStem.length),
  };
}

function parseTagalogSandwichOToU(rule) {
  const match = rule.isInflected.source.match(/^\^(.*)\(\\w\+\)u\(\[[^\]]+\]\+\)(.*)\$$/);
  if (!match) {
    return null;
  }
  const prefix = regexLiteralToString(match[1]);
  const suffix = regexLiteralToString(match[2]);
  const input = `${prefix}rootuxt${suffix}`;
  const output = rule.deinflect(input);
  const middle = 'rootoxt';
  const index = output.indexOf(middle);
  if (index < 0) {
    throw new Error(`Could not infer Tagalog sandwich O-to-U from ${rule.isInflected}`);
  }
  return {
    kind: KINDS.tagalogSandwichOToU,
    prefix,
    deinflectedPrefix: output.slice(0, index),
    suffix,
    deinflectedSuffix: output.slice(index + middle.length),
  };
}

function parseYiddishUmlaut(rule) {
  if (!functionSource(rule).includes('mutation.new')) {
    return null;
  }
  const suffix = regexLiteralToString(rule.isInflected.source.replace(/\$$/, ''));
  for (const mutation of YIDDISH_MUTATIONS) {
    const output = rule.deinflect(`${mutation.new}${suffix}`);
    if (output === `${mutation.orig}${rule.deinflected}`) {
      return {
        kind: KINDS.yiddishUmlautSuffix,
        suffix,
        deinflectedSuffix: rule.deinflected,
        inflectedChar: mutation.new,
        deinflectedChar: mutation.orig,
      };
    }
  }
  throw new Error(`Could not infer Yiddish umlaut mutation from ${rule.isInflected}`);
}

function convertRule(language, rule) {
  const source = functionSource(rule);

  if (language === 'de' && source.includes("term.replace(regex, '$1 ' + prefix)")) {
    const prefix = regexLiteralToString(rule.isInflected.source.replace(/^\^\(\[.*\]\+\) \.\+ /, '').replace(/\$$/, ''));
    return [{kind: KINDS.germanSeparatedPrefix, a: prefix}];
  }
  if (language === 'de' && source.includes('regularPastParticiple')) {
    const suffix = rule.deinflect('gearbeitet').slice('arbeit'.length);
    return [{kind: KINDS.germanBasicPastParticiple, b: suffix}];
  }
  if (language === 'de' && source.includes('separablePastParticiple')) {
    return parseGermanSeparablePastParticiple(rule).map((r) => ({kind: r.kind, a: r.prefix, b: r.suffix}));
  }
  if (language === 'el' && source.includes("normalize('NFD')")) {
    return [{kind: KINDS.greekXiAnaNormalize, a: 'ξανα'}];
  }
  if (language === 'en' && source.includes('phrasalVerbWordDisjunction') && source.includes('inflected')) {
    const parsed = parseEnglishPhrasalSuffix(rule);
    return [{kind: parsed.kind, a: parsed.inflectedSuffix, b: parsed.deinflectedSuffix}];
  }
  if (language === 'en' && source.includes('phrasalVerbWordDisjunction') && source.includes("return term.replace(new RegExp(`(?<=\\\\w)")) {
    return [{kind: KINDS.englishInterposedPhrasalObject}];
  }
  if (language === 'es' && source.includes('REFLEXIVE_PATTERN')) {
    return [{kind: KINDS.spanishPronominal}];
  }
  if (language === 'es' && rule.type === 'other') {
    const parsed = parseSpanishJugarStem(rule) ?? parseSpanishOlerStem(rule) ?? parseSpanishSimpleStem(rule);
    if (parsed) {
      return [{
        kind: parsed.kind,
        a: parsed.inflectedStem,
        b: parsed.deinflectedStem,
        c: parsed.suffixes,
        d: parsed.deinflectedSuffix,
        e: parsed.specialSuffixes ?? '',
      }];
    }
  }
  if (language === 'sga' && source.includes('orthographyRegExp')) {
    const notAtStart = rule.isInflected.source.startsWith('(?<!^)');
    const literal = regexLiteralToString(rule.isInflected.source.replace(/^\(\?<!\^\)/, ''));
    const output = rule.deinflect(`${notAtStart ? 'x' : ''}${literal}`);
    return [{
      kind: notAtStart ? KINDS.replaceAllNotAtStart : KINDS.replaceAll,
      a: literal,
      b: notAtStart ? output.slice(1) : output,
    }];
  }
  if (language === 'tl' && source === "(text) => text.replace(regex, '')") {
    return [{kind: KINDS.tagalogRemoveFirst, a: '-'}];
  }
  if (language === 'tl' && source.includes('o$1')) {
    const parsed = parseTagalogSuffixOToU(rule);
    return [{kind: parsed.kind, a: parsed.suffix, b: parsed.deinflectedSuffix}];
  }
  if (language === 'tl' && source.includes('deinflectedPrefix}$2')) {
    const parsed = parseTagalogPrefixReduplication(rule);
    return [{kind: parsed.kind, a: parsed.prefix, b: parsed.deinflectedPrefix, c: parsed.consonants}];
  }
  if (language === 'tl' && source.includes('$1o$2')) {
    const parsed = parseTagalogSandwichOToU(rule);
    return [{kind: parsed.kind, a: parsed.prefix, b: parsed.deinflectedPrefix, c: parsed.suffix, d: parsed.deinflectedSuffix}];
  }
  if (language === 'yi') {
    const parsed = parseYiddishUmlaut(rule);
    if (parsed) {
      return [{kind: parsed.kind, a: parsed.suffix, b: parsed.deinflectedSuffix, c: parsed.inflectedChar, d: parsed.deinflectedChar}];
    }
  }

  if (rule.type === 'wholeWord') {
    return [{kind: KINDS.wholeWord, a: literalWholeWordFromRegex(rule), b: rule.deinflect('')}];
  }
  if (rule.type === 'prefix' && source === '(text) => deinflectedPrefix + text.slice(inflectedPrefix.length)') {
    const parsed = inferPrefix(rule);
    return [{
      kind: parsed.initialStemSegment ? KINDS.guardedPrefix : KINDS.prefix,
      a: parsed.inflectedPrefix,
      b: parsed.deinflectedPrefix,
      e: parsed.initialStemSegment,
    }];
  }
  if ((rule.type === 'suffix' || rule.type === 'other') && hasOwn(rule, 'deinflected')) {
    const parsed = inferSuffix(rule);
    return [{
      kind: parsed.kind,
      a: parsed.suffix,
      b: parsed.deinflectedSuffix,
      e: parsed.finalStemSegment ?? '',
    }];
  }
  if (rule.type === 'other' && source === '(text) => deinflectedPrefix + text.slice(inflectedPrefix.length, -inflectedSuffix.length) + deinflectedSuffix') {
    const parsed = inferSandwich(rule, language);
    return [{
      kind: parsed.kind,
      a: parsed.inflectedPrefix,
      b: parsed.deinflectedPrefix,
      c: parsed.inflectedSuffix,
      d: parsed.deinflectedSuffix,
      e: parsed.initialStemSegment,
      f: parsed.finalStemSegment,
    }];
  }

  throw new Error(`Unhandled rule: ${language} ${rule.type} ${rule.isInflected} ${source}`);
}

function buildConditionFlags(conditions) {
  const names = Object.keys(conditions);
  let nextBit = 1;
  const leafFlags = new Map();
  const resolved = new Map();

  function resolve(name) {
    if (resolved.has(name)) {
      return resolved.get(name);
    }
    const condition = conditions[name];
    if (!condition) {
      throw new Error(`Unknown condition ${name}`);
    }
    let flags = 0;
    if (condition.subConditions?.length) {
      for (const subCondition of condition.subConditions) {
        flags |= resolve(subCondition);
      }
    } else {
      flags = nextBit;
      leafFlags.set(name, flags);
      nextBit <<= 1;
    }
    resolved.set(name, flags);
    return flags;
  }

  for (const name of names) {
    resolve(name);
  }

  return names.map((name) => ({
    name,
    flags: resolved.get(name),
    isDictionaryForm: conditions[name].isDictionaryForm === true,
  }));
}

function flagsFor(conditionFlags, names) {
  let result = 0;
  for (const name of names ?? []) {
    const condition = conditionFlags.find((entry) => entry.name === name);
    if (!condition) {
      throw new Error(`Unknown condition in rule: ${name}`);
    }
    result |= condition.flags;
  }
  return result;
}

function cppString(value = '') {
  const escaped = value
    .replace(/\\/g, '\\\\')
    .replace(/"/g, '\\"')
    .replace(/\r/g, '\\r')
    .replace(/\n/g, '\\n')
    .replace(/\t/g, '\\t');
  return `std::string_view{"${escaped}"}`;
}

function cppIdentifier(language) {
  return language.replace(/[^a-zA-Z0-9_]/g, '_');
}

function emitArray(name, type, items, emitItem) {
  const lines = [`static constexpr std::array<${type}, ${items.length}> ${name} = {{`];
  for (const item of items) {
    lines.push(`    ${emitItem(item)},`);
  }
  lines.push('}};');
  return lines.join('\n');
}

function buildLanguage(descriptor) {
  const language = descriptor.language;
  const conditions = buildConditionFlags(descriptor.conditions ?? {});
  const transforms = [];
  const rules = [];

  for (const [id, transform] of Object.entries(descriptor.transforms ?? {})) {
    const transformIndex = transforms.length;
    transforms.push({
      id,
      name: transform.name ?? id,
      description: transform.description ?? '',
    });
    for (const rule of transform.rules ?? []) {
      const convertedRules = convertRule(language, rule);
      for (const converted of convertedRules) {
        rules.push({
          kind: converted.kind,
          a: converted.a ?? '',
          b: converted.b ?? '',
          c: converted.c ?? '',
          d: converted.d ?? '',
          e: converted.e ?? '',
          f: converted.f ?? '',
          conditionsIn: flagsFor(conditions, rule.conditionsIn),
          conditionsOut: flagsFor(conditions, rule.conditionsOut),
          transformIndex,
        });
      }
    }
  }

  return {language, conditions, transforms, rules};
}

const languages = descriptors.map(buildLanguage);

const output = [];
output.push('#include "language_data.hpp"');
output.push('');
output.push('#include <array>');
output.push('');
output.push('namespace language_data {');
output.push('');
output.push('using enum RuleKind;');
output.push('');

for (const language of languages) {
  const suffix = cppIdentifier(language.language);
  output.push(emitArray(`conditions_${suffix}`, 'Condition', language.conditions, (condition) =>
    `{${cppString(condition.name)}, ${condition.flags}u, ${condition.isDictionaryForm ? 'true' : 'false'}}`));
  output.push('');
  output.push(emitArray(`transforms_${suffix}`, 'Transform', language.transforms, (transform) =>
    `{${cppString(transform.id)}, ${cppString(transform.name)}, ${cppString(transform.description)}}`));
  output.push('');
  output.push(emitArray(`rules_${suffix}`, 'Rule', language.rules, (rule) =>
    `{${rule.kind}, ${cppString(rule.a)}, ${cppString(rule.b)}, ${cppString(rule.c)}, ${cppString(rule.d)}, ` +
    `${cppString(rule.e)}, ${cppString(rule.f)}, ${rule.conditionsIn}u, ${rule.conditionsOut}u, ${rule.transformIndex}}`));
  output.push('');
}

output.push(`static constexpr std::array<Language, ${languages.length}> languages = {{`);
for (const language of languages) {
  const suffix = cppIdentifier(language.language);
  output.push(`    {${cppString(language.language)}, conditions_${suffix}, transforms_${suffix}, rules_${suffix}},`);
}
output.push('}};');
output.push('');
output.push('std::span<const Language> all_languages() { return languages; }');
output.push('');
output.push('const Language* find_language(std::string_view code) {');
output.push('  if (code == "ka") { code = "kat"; }');
output.push('  if (code == "arz") { code = "ar"; }');
output.push('  for (const auto& language : languages) {');
output.push('    if (language.code == code) { return &language; }');
output.push('  }');
output.push('  return nullptr;');
output.push('}');
output.push('');
output.push('}  // namespace language_data');
output.push('');

const outputPath = path.join(repoRoot, 'src/language/generated_language_data.cpp');
fs.writeFileSync(outputPath, output.join('\n'), 'utf8');

for (const language of languages) {
  console.log(`${language.language}: ${language.rules.length} rules`);
}
