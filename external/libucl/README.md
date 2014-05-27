# LIBUCL

[![Build Status](https://travis-ci.org/vstakhov/libucl.svg?branch=master)](https://travis-ci.org/vstakhov/libucl)

**Table of Contents**  *generated with [DocToc](http://doctoc.herokuapp.com/)*

- [Introduction](#introduction)
- [Basic structure](#basic-structure)
- [Improvements to the json notation](#improvements-to-the-json-notation)
	- [General syntax sugar](#general-syntax-sugar)
	- [Automatic arrays creation](#automatic-arrays-creation)
	- [Named keys hierarchy](#named-keys-hierarchy)
	- [Convenient numbers and booleans](#convenient-numbers-and-booleans)
- [General improvements](#general-improvements)
	- [Commments](#commments)
	- [Macros support](#macros-support)
	- [Variables support](#variables-support)
	- [Multiline strings](#multiline-strings)
- [Emitter](#emitter)
- [Validation](#validation)
- [Performance](#performance)
- [Conclusion](#conclusion)

## Introduction

This document describes the main features and principles of the configuration
language called `UCL` - universal configuration language.

If you are looking for the libucl API documentation you can find it at [this page](doc/api.md).

## Basic structure

UCL is heavily infused by `nginx` configuration as the example of a convenient configuration
system. However, UCL is fully compatible with `JSON` format and is able to parse json files.
For example, you can write the same configuration in the following ways:

* in nginx like:

```nginx
param = value;
section {
    param = value;
    param1 = value1;
    flag = true;
    number = 10k;
    time = 0.2s;
    string = "something";
    subsection {
        host = {
            host = "hostname"; 
            port = 900;
        }
        host = {
            host = "hostname";
            port = 901;
        }
    }
}
```

* or in JSON:

```json
{
    "param": "value",
    "param1": "value1",
    "flag": true,
    "subsection": {
        "host": [
        {
            "host": "hostname",
            "port": 900
        },
        {
            "host": "hostname",
            "port": 901
        }
        ]
    }
}
```

## Improvements to the json notation.

There are various things that make ucl configuration more convenient for editing than strict json:

### General syntax sugar

* Braces are not necessary to enclose a top object: it is automatically treated as an object:

```json
"key": "value"
```
is equal to:
```json
{"key": "value"}
```

* There is no requirement of quotes for strings and keys, moreover, `:` may be replaced `=` or even be skipped for objects:

```nginx
key = value;
section {
    key = value;
}
```
is equal to:
```json
{
    "key": "value",
    "section": {
        "key": "value"
    }
}
```

* No commas mess: you can safely place a comma or semicolon for the last element in an array or an object:

```json
{
    "key1": "value",
    "key2": "value",
}
```
### Automatic arrays creation

* Non-unique keys in an object are allowed and are automatically converted to the arrays internally:

```json
{
    "key": "value1",
    "key": "value2"
}
```
is converted to:
```json
{
    "key": ["value1", "value2"]
}
```

### Named keys hierarchy

UCL accepts named keys and organize them into objects hierarchy internally. Here is an example of this process:
```nginx
section "blah" {
	key = value;
}
section foo {
	key = value;
}
```

is converted to the following object:

```nginx
section {
	blah {
			key = value;
	}
	foo {
			key = value;
	}
}
```
    
Plain definitions may be more complex and contain more than a single level of nested objects:
   
```nginx
section "blah" "foo" {
	key = value;
}
```

is presented as:

```nginx    
section {
	blah {
			foo {
					key = value;
			}
	}
}
```

### Convenient numbers and booleans

* Numbers can have suffixes to specify standard multipliers:
    + `[kKmMgG]` - standard 10 base multipliers (so `1k` is translated to 1000)
    + `[kKmMgG]b` - 2 power multipliers (so `1kb` is translated to 1024)
    + `[s|min|d|w|y]` - time multipliers, all time values are translated to float number of seconds, for example `10min` is translated to 600.0 and `10ms` is translated to 0.01
* Hexadecimal integers can be used by `0x` prefix, for example `key = 0xff`. However, floating point values can use decimal base only.
* Booleans can be specified as `true` or `yes` or `on` and `false` or `no` or `off`.
* It is still possible to treat numbers and booleans as strings by enclosing them in double quotes.

## General improvements

### Commments

UCL supports different style of comments:

* single line: `#` 
* multiline: `/* ... */`

Multiline comments may be nested:
```c
# Sample single line comment
/* 
 some comment
 /* nested comment */
 end of comment
*/
```

### Macros support

UCL supports external macros both multiline and single line ones:
```nginx
.macro "sometext";
.macro {
     Some long text
     ....
};
```
There are two internal macros provided by UCL:

* `include` - read a file `/path/to/file` or an url `http://example.com/file` and include it to the current place of
UCL configuration;
* `try\_include` - try to read a file or url and include it but do not create a fatal error if a file or url is not accessible;
* `includes` - read a file or an url like the previous macro, but fetch and check the signature file (which is obtained
by `.sig` suffix appending).

Public keys which are used for the last command are specified by the concrete UCL user.

### Variables support

UCL supports variables in input. Variables are registered by a user of the UCL parser and can be presented in the following forms:

* `${VARIABLE}`
* `$VARIABLE`

UCL currently does not support nested variables. To escape variables one could use double dollar signs:

* `$${VARIABLE}` is converted to `${VARIABLE}`
* `$$VARIABLE` is converted to `$VARIABLE`

However, if no valid variables are found in a string, no expansion will be performed (and `$$` thus remains unchanged). This may be a subject
to change in future libucl releases.

### Multiline strings

UCL can handle multiline strings as well as single line ones. It uses shell/perl like notation for such objects:
```
key = <<EOD
some text
splitted to
lines
EOD
```

In this example `key` will be interpreted as the following string: `some text\nsplitted to\nlines`.
Here are some rules for this syntax:

* Multiline terminator must start just after `<<` symbols and it must consist of capital letters only (e.g. `<<eof` or `<< EOF` won't work);
* Terminator must end with a single newline character (and no spaces are allowed between terminator and newline character);
* To finish multiline string you need to include a terminator string just after newline and followed by a newline (no spaces or other characters are allowed as well);
* The initial and the final newlines are not inserted to the resulting string, but you can still specify newlines at the begin and at the end of a value, for example:

```
key <<EOD

some
text

EOD
```

## Emitter

Each UCL object can be serialized to one of the three supported formats:

* `JSON` - canonic json notation (with spaces indented structure);
* `Compacted JSON` - compact json notation (without spaces or newlines);
* `Configuration` - nginx like notation;
* `YAML` - yaml inlined notation.

## Validation

UCL allows validation of objects. It uses the same schema that is used for json: [json schema v4](http://json-schema.org). UCL supports the full set of json schema with the exception of remote references. This feature is unlikely useful for configuration objects. Of course, a schema definition can be in UCL format instead of JSON that simplifies schemas writing. Moreover, since UCL supports multiple values for keys in an object it is possible to specify generic integer constraints `maxValues` and `minValues` to define the limits of values count in a single key. UCL currently is not absolutely strict about validation schemas themselves, therefore UCL users should supply valid schemas (as it is defined in json-schema draft v4) to ensure that the input objects are validated properly.

## Performance

Are UCL parser and emitter fast enough? Well, there are some numbers.
I got a 19Mb file that consist of ~700 thousands lines of json (obtained via
http://www.json-generator.com/). Then I checked jansson library that performs json
parsing and emitting and compared it with UCL. Here are results:

```
jansson: parsed json in 1.3899 seconds
jansson: emitted object in 0.2609 seconds

ucl: parsed input in 0.6649 seconds
ucl: emitted config in 0.2423 seconds
ucl: emitted json in 0.2329 seconds
ucl: emitted compact json in 0.1811 seconds
ucl: emitted yaml in 0.2489 seconds
```

So far, UCL seems to be significantly faster than jansson on parsing and slightly faster on emitting. Moreover,
UCL compiled with optimizations (-O3) performs significantly faster:
```
ucl: parsed input in 0.3002 seconds
ucl: emitted config in 0.1174 seconds
ucl: emitted json in 0.1174 seconds
ucl: emitted compact json in 0.0991 seconds
ucl: emitted yaml in 0.1354 seconds
```

You can do your own benchmarks by running `make test` in libucl top directory.

## Conclusion

UCL has clear design that should be very convenient for reading and writing. At the same time it is compatible with
JSON language and therefore can be used as a simple JSON parser. Macroes logic provides an ability to extend configuration
language (for example by including some lua code) and comments allows to disable or enable the parts of a configuration
quickly.
