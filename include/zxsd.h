/*
 * Copyright (c) 2026 Sixtaupi
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Public API for the zxsd XML validation runtime.
 */

#ifndef ZXSD_H
#define ZXSD_H

#include <stddef.h>
#include <stdint.h>

#include "zxsd_version.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup zxsd_api zxsd validation API
 * @brief Validate XML documents with generated XSD descriptors.
 * @{
 */

#ifndef ZXSD_MAX_INPUT
/** Maximum accepted XML input size, in bytes. */
#define ZXSD_MAX_INPUT (1024u * 1024u)
#endif
#ifndef ZXSD_MAX_DEPTH
/** Maximum nesting depth accepted by the embedded XML parser. */
#define ZXSD_MAX_DEPTH 32u
#endif
#ifndef ZXSD_MAX_PARTICLES
/** Maximum number of child particles in one complex type. */
#define ZXSD_MAX_PARTICLES 32u
#endif
#if ZXSD_MAX_PARTICLES > 65536u
#error "ZXSD_MAX_PARTICLES exceeds the 16-bit streaming particle-token space"
#endif
#ifndef ZXSD_MAX_ATTRIBUTES
/** Maximum number of attributes in one element. */
#define ZXSD_MAX_ATTRIBUTES 32u
#endif
#ifndef ZXSD_MAX_NAMESPACES
/** Maximum number of simultaneously active namespace bindings. */
#define ZXSD_MAX_NAMESPACES 16u
#endif
#ifndef ZXSD_MAX_IDENTITY_ROWS
/** Maximum stored tuples per identity constraint. */
#define ZXSD_MAX_IDENTITY_ROWS 64u
#endif
#ifndef ZXSD_MAX_IDENTITY_VALUES
/** Maximum identity values, retained as a compatibility alias for rows. */
#define ZXSD_MAX_IDENTITY_VALUES ZXSD_MAX_IDENTITY_ROWS
#endif
#ifndef ZXSD_MAX_IDENTITY_TEXT
/** Maximum total UTF-8 bytes retained for document ID and IDREF values. */
#define ZXSD_MAX_IDENTITY_TEXT 4096u
#endif
#ifndef ZXSD_MAX_IDENTITY_CONSTRAINTS
/** Maximum identity constraints attached to one root declaration. */
#define ZXSD_MAX_IDENTITY_CONSTRAINTS 16u
#endif
#ifndef ZXSD_MAX_IDENTITY_FIELDS
/** Maximum fields in one identity-constraint tuple. */
#define ZXSD_MAX_IDENTITY_FIELDS 8u
#endif
#ifndef ZXSD_MAX_XPATH_DEPTH
/** Maximum number of steps in a compiled identity XPath. */
#define ZXSD_MAX_XPATH_DEPTH 8u
#endif
#ifndef ZXSD_MAX_XPATH_ALTERNATIVES
/** Maximum union branches in one compiled identity XPath. */
#define ZXSD_MAX_XPATH_ALTERNATIVES 8u
#endif
#ifndef ZXSD_STREAM_TEXT_CAPACITY
/** Maximum decoded character data retained for one streamed element. */
#define ZXSD_STREAM_TEXT_CAPACITY ZXSD_MAX_TEXT_LENGTH
#endif
#ifndef ZXSD_MAX_TEXT_LENGTH
/** Maximum lexical length of one element or attribute value. */
#define ZXSD_MAX_TEXT_LENGTH 1024u
#endif
#ifndef ZXSD_MAX_PATTERN_LENGTH
/** Maximum UTF-8 byte length of one generated XSD regular expression. */
#define ZXSD_MAX_PATTERN_LENGTH 4096u
#endif
#ifndef ZXSD_MAX_REGEX_NODES
/** Maximum parsed expression nodes used by the allocation-free regex engine. */
#define ZXSD_MAX_REGEX_NODES 512u
#endif
#ifndef ZXSD_MAX_REGEX_DEPTH
/** Maximum parenthesis and character-class subtraction nesting depth. */
#define ZXSD_MAX_REGEX_DEPTH 32u
#endif
#ifndef ZXSD_MAX_SCHEMA_COMPLEXITY
/** Maximum aggregate descriptor count accepted by the runtime. */
#define ZXSD_MAX_SCHEMA_COMPLEXITY 4096u
#endif
#ifndef ZXSD_STREAM_NAME_CAPACITY
/** Maximum local-name or namespace-URI length accepted by streaming calls. */
#define ZXSD_STREAM_NAME_CAPACITY 128u
#endif
#ifndef ZXSD_STREAM_CHILD_CAPACITY
/** Maximum child tokens retained for a streamed model requiring history.
 * Incremental models use counters and do not consume this capacity.
 */
#define ZXSD_STREAM_CHILD_CAPACITY 64u
#endif
#ifndef ZXSD_STREAM_MODEL_CAPACITY
/** Shared continuation/state slots for nested streaming content models.
 * Storage is shared by all open elements, not multiplied by XML depth.
 * Exhaustion returns @ref ZXSD_ERR_STREAM_LIMIT. Zero selects child history
 * instead; flat models always retain their allocation-free fast path.
 */
#define ZXSD_STREAM_MODEL_CAPACITY 128u
#endif
#ifndef ZXSD_XML_TOKEN_CAPACITY
/** Maximum bytes retained for one incomplete XML tag, comment, PI, or CDATA section. */
#define ZXSD_XML_TOKEN_CAPACITY 4096u
#endif

/** Validation status and error codes. */
typedef enum {
	ZXSD_OK = 0,                   /**< Validation succeeded. */
	ZXSD_ERR_ARGUMENT,             /**< A required argument is invalid. */
	ZXSD_ERR_INPUT_TOO_LARGE,      /**< XML exceeds @ref ZXSD_MAX_INPUT. */
	ZXSD_ERR_MALFORMED_XML,        /**< XML syntax is malformed. */
	ZXSD_ERR_EXCESSIVE_NESTING,    /**< XML nesting exceeds the limit. */
	ZXSD_ERR_WRONG_ROOT,           /**< The document root does not match. */
	ZXSD_ERR_MISSING_ELEMENT,      /**< A required child element is absent. */
	ZXSD_ERR_UNEXPECTED_ELEMENT,   /**< A child element is not permitted. */
	ZXSD_ERR_ELEMENT_ORDER,        /**< Sequence order is incorrect. */
	ZXSD_ERR_OCCURRENCE,           /**< An occurrence bound was violated. */
	ZXSD_ERR_MISSING_ATTRIBUTE,    /**< A required attribute is absent. */
	ZXSD_ERR_UNEXPECTED_ATTRIBUTE, /**< An attribute is not permitted. */
	ZXSD_ERR_INVALID_VALUE,        /**< A primitive or restricted value is invalid. */
	ZXSD_ERR_TEXT_NOT_ALLOWED,     /**< Text occurred in element-only content. */
	ZXSD_ERR_IDENTITY_CONSTRAINT,  /**< A unique, key or keyref rule failed. */
	ZXSD_ERR_STREAM_LIMIT,         /**< A fixed streaming buffer was exceeded. */
	ZXSD_ERR_RESOURCE_LIMIT,       /**< A configured resource limit was exceeded. */
	ZXSD_ERR_OUTPUT,               /**< A binding output sink or buffer failed. */
	ZXSD_ERR_UNSUPPORTED,          /**< A valid document cannot be represented by a binding. */
	ZXSD_ERR_INCOMPATIBLE_ABI      /**< Runtime epochs, limits, or public layouts differ. */
} zxsd_error_t;

/** Structured result returned by every generated validator. */
typedef struct {
	zxsd_error_t code;   /**< Validation status. */
	size_t offset;       /**< Byte offset associated with the result. */
	const char *path;    /**< Schema item involved, or @c NULL. */
	const char *message; /**< Static, human-readable status text. */
} zxsd_result_t;

/**
 * @brief Allocate runtime scratch storage.
 *
 * @param[in] context Opaque allocator context.
 * @param[in] size Number of bytes to allocate.
 *
 * @return Pointer to the allocated storage on success.
 * @retval NULL If the storage could not be allocated.
 */
typedef void *(*zxsd_allocate_fn)(void *context, size_t size);

/**
 * @brief Release runtime scratch storage.
 *
 * @param[in] context Opaque allocator context.
 * @param[in] pointer Storage previously returned by @ref zxsd_allocate_fn.
 */
typedef void (*zxsd_deallocate_fn)(void *context, void *pointer);

/** Optional allocator used by validation features requiring scratch storage. */
typedef struct {
	zxsd_allocate_fn allocate;     /**< Allocation callback. */
	zxsd_deallocate_fn deallocate; /**< Matching release callback. */
	void *context;                 /**< Opaque callback context. */
} zxsd_allocator_t;

/** Primitive and compound value kinds understood by the runtime. */
typedef enum {
	ZXSD_T_STRING,               /**< XML Schema string. */
	ZXSD_T_BOOLEAN,              /**< XML Schema boolean. */
	ZXSD_T_INT,                  /**< Signed 32-bit XML Schema integer. */
	ZXSD_T_UNSIGNED_INT,         /**< Unsigned 32-bit XML Schema integer. */
	ZXSD_T_INTEGER,              /**< Arbitrary-width integer lexical form. */
	ZXSD_T_DECIMAL,              /**< XML Schema decimal. */
	ZXSD_T_FLOAT,                /**< XML Schema float. */
	ZXSD_T_DOUBLE,               /**< XML Schema double. */
	ZXSD_T_LONG,                 /**< Signed 64-bit XML Schema integer. */
	ZXSD_T_SHORT,                /**< Signed 16-bit XML Schema integer. */
	ZXSD_T_BYTE,                 /**< Signed 8-bit XML Schema integer. */
	ZXSD_T_NON_POSITIVE_INTEGER, /**< Integer less than or equal to zero. */
	ZXSD_T_NEGATIVE_INTEGER,     /**< Integer less than zero. */
	ZXSD_T_NON_NEGATIVE_INTEGER, /**< Integer greater than or equal to zero. */
	ZXSD_T_POSITIVE_INTEGER,     /**< Integer greater than zero. */
	ZXSD_T_UNSIGNED_LONG,        /**< Unsigned 64-bit XML Schema integer. */
	ZXSD_T_UNSIGNED_SHORT,       /**< Unsigned 16-bit XML Schema integer. */
	ZXSD_T_UNSIGNED_BYTE,        /**< Unsigned 8-bit XML Schema integer. */
	ZXSD_T_NORMALIZED_STRING,    /**< XML Schema normalizedString. */
	ZXSD_T_TOKEN,                /**< XML Schema token. */
	ZXSD_T_LANGUAGE,             /**< XML Schema language. */
	ZXSD_T_NAME,                 /**< XML Schema Name. */
	ZXSD_T_NCNAME,               /**< XML Schema NCName. */
	ZXSD_T_NMTOKEN,              /**< XML Schema NMTOKEN. */
	ZXSD_T_QNAME,                /**< XML Schema QName. */
	ZXSD_T_ANY_URI,              /**< XML Schema anyURI. */
	ZXSD_T_DURATION,             /**< XML Schema duration. */
	ZXSD_T_DATE,                 /**< XML Schema date. */
	ZXSD_T_DATE_TIME,            /**< XML Schema dateTime. */
	ZXSD_T_TIME,                 /**< XML Schema time. */
	ZXSD_T_GYEAR,                /**< XML Schema gYear. */
	ZXSD_T_GYEAR_MONTH,          /**< XML Schema gYearMonth. */
	ZXSD_T_GMONTH,               /**< XML Schema gMonth. */
	ZXSD_T_GMONTH_DAY,           /**< XML Schema gMonthDay. */
	ZXSD_T_GDAY,                 /**< XML Schema gDay. */
	ZXSD_T_HEX_BINARY,           /**< XML Schema hexBinary. */
	ZXSD_T_BASE64_BINARY,        /**< XML Schema base64Binary. */
	ZXSD_T_ID,                   /**< XML Schema ID. */
	ZXSD_T_IDREF,                /**< XML Schema IDREF. */
	ZXSD_T_ENTITY,               /**< XML Schema ENTITY. */
	ZXSD_T_NOTATION,             /**< XML Schema NOTATION QName. */
	ZXSD_T_COMPLEX               /**< Element-only complex content. */
} zxsd_type_t;

/** Whitespace treatment prescribed by an XML Schema simple type. */
typedef enum {
	ZXSD_WS_PRESERVE = 0, /**< Keep every character unchanged. */
	ZXSD_WS_REPLACE,      /**< Replace tab, carriage return and line feed. */
	ZXSD_WS_COLLAPSE      /**< Replace, trim and collapse whitespace runs. */
} zxsd_whitespace_t;

/** Variety of an XML Schema simple type. */
typedef enum {
	ZXSD_SIMPLE_ATOMIC = 0, /**< One atomic value. */
	ZXSD_SIMPLE_LIST,       /**< Whitespace-separated item sequence. */
	ZXSD_SIMPLE_UNION       /**< Value accepted by at least one member type. */
} zxsd_simple_variety_t;

/** Complex content model kind. */
typedef enum {
	ZXSD_SEQUENCE = 0, /**< Children occur in declaration order. */
	ZXSD_CHOICE,       /**< Children select from alternatives. */
	ZXSD_ALL           /**< Children may occur in any order. */
} zxsd_model_t;

/** Wildcard validation behavior. */
typedef enum {
	ZXSD_PROCESS_STRICT = 0, /**< A matching declaration is required. */
	ZXSD_PROCESS_LAX,        /**< Validate when a declaration is available. */
	ZXSD_PROCESS_SKIP        /**< Accept the matching subtree without validation. */
} zxsd_process_t;

/** Compiled content-model particle kind. */
typedef enum {
	ZXSD_PARTICLE_ELEMENT = 0, /**< Element declaration. */
	ZXSD_PARTICLE_SEQUENCE,    /**< Ordered child particles. */
	ZXSD_PARTICLE_CHOICE,      /**< One alternative. */
	ZXSD_PARTICLE_ALL,         /**< Unordered child particles. */
	ZXSD_PARTICLE_ANY          /**< Element wildcard. */
} zxsd_particle_kind_t;

/** Simple-type facet flags used by @ref zxsd_simple_t. */
enum {
	ZXSD_F_LENGTH = 1u << 0,         /**< Exact length is active. */
	ZXSD_F_MIN_LENGTH = 1u << 1,     /**< Minimum length is active. */
	ZXSD_F_MAX_LENGTH = 1u << 2,     /**< Maximum length is active. */
	ZXSD_F_MIN_INCLUSIVE = 1u << 3,  /**< Inclusive lower bound is active. */
	ZXSD_F_MAX_INCLUSIVE = 1u << 4,  /**< Inclusive upper bound is active. */
	ZXSD_F_MIN_EXCLUSIVE = 1u << 5,  /**< Exclusive lower bound is active. */
	ZXSD_F_MAX_EXCLUSIVE = 1u << 6,  /**< Exclusive upper bound is active. */
	ZXSD_F_PATTERN = 1u << 7,        /**< Pattern constraint is active. */
	ZXSD_F_TOTAL_DIGITS = 1u << 8,   /**< Maximum decimal digits is active. */
	ZXSD_F_FRACTION_DIGITS = 1u << 9 /**< Maximum fractional digits is active. */
};

/** Type-derivation methods used by block and final. */
enum {
	ZXSD_DERIVATION_EXTENSION = 1u << 0,    /**< Complex extension. */
	ZXSD_DERIVATION_RESTRICTION = 1u << 1,  /**< Simple or complex restriction. */
	ZXSD_DERIVATION_SUBSTITUTION = 1u << 2, /**< Element substitution group. */
	ZXSD_DERIVATION_LIST = 1u << 3,         /**< Simple list derivation. */
	ZXSD_DERIVATION_UNION = 1u << 4,        /**< Simple union derivation. */
	ZXSD_DERIVATION_ALL = 0x1fu             /**< Every supported method. */
};

struct zxsd_simple;

/** Reference to a built-in or generated simple type. */
typedef struct {
	zxsd_type_t type;                 /**< Built-in primitive or derived kind. */
	const struct zxsd_simple *simple; /**< Generated descriptor, or @c NULL. */
} zxsd_simple_ref_t;

/** Generated descriptor for a restricted, list or union simple type. */
typedef struct zxsd_simple {
	zxsd_type_t base;                          /**< Primitive base type. */
	unsigned facets;                           /**< Bitwise combination of @c ZXSD_F_*. */
	size_t length;                             /**< Exact character length. */
	size_t min_length;                         /**< Minimum character length. */
	size_t max_length;                         /**< Maximum character length. */
	const char *min_inclusive;                 /**< Inclusive lower-bound text. */
	const char *max_inclusive;                 /**< Inclusive upper-bound text. */
	const char *min_exclusive;                 /**< Exclusive lower-bound text. */
	const char *max_exclusive;                 /**< Exclusive upper-bound text. */
	const char *pattern;                       /**< Embedded pattern expression. */
	const char *const *enumeration;            /**< Permitted lexical values. */
	size_t enumeration_count;                  /**< Number of enumeration values. */
	zxsd_whitespace_t whitespace;              /**< Normalization applied before validation. */
	zxsd_simple_variety_t variety;             /**< Atomic, list or union variety. */
	zxsd_simple_ref_t item_type;               /**< Item type for @ref ZXSD_SIMPLE_LIST. */
	const zxsd_simple_ref_t *member_types;     /**< Union member types. */
	size_t member_type_count;                  /**< Number of union member types. */
	const struct zxsd_simple *base_definition; /**< Named base definition. */
	unsigned derivation_method;                /**< Method used to derive this type. */
	unsigned final_methods;                    /**< Methods forbidden to descendants. */
	const char *type_name;                     /**< Named type local name, or @c NULL. */
	const char *type_namespace;                /**< Named type namespace, or @c NULL. */
	const char *const *patterns; /**< Pattern constraints from each restriction. */
	size_t pattern_count;        /**< Number of conjunctive pattern constraints. */
	/** Namespace URI for each QName/NOTATION enumeration, or @c NULL. */
	const char *const *enumeration_namespaces;
	size_t total_digits;    /**< Maximum significant decimal digits. */
	size_t fraction_digits; /**< Maximum fractional decimal digits. */
} zxsd_simple_t;

struct zxsd_complex;
struct zxsd_particle;

/** Generated descriptor for an element particle. */
typedef struct {
	const char *name;                        /**< Local element name. */
	zxsd_type_t type;                        /**< Value kind. */
	const struct zxsd_complex *complex_type; /**< Complex descriptor, if any. */
	unsigned long min_occurs;                /**< Minimum occurrences. */
	unsigned long max_occurs;                /**< Maximum, or @c ULONG_MAX. */
	const zxsd_simple_t *simple_type;        /**< Simple restriction, if any. */
	const char *namespace_uri;               /**< Required namespace, or @c NULL. */
	const char *default_value;               /**< Schema default, or @c NULL. */
	const char *fixed_value;                 /**< Required fixed value, or @c NULL. */
	int nillable;                            /**< Non-zero when xsi:nil is allowed. */
	int abstract_element;                    /**< Non-zero when substitution is required. */
	unsigned block_methods;                  /**< Prohibited runtime substitutions. */
	const char *substitution_head_name;      /**< Head local name, or @c NULL. */
	const char *substitution_head_namespace; /**< Head namespace, or @c NULL. */
} zxsd_element_t;

/** Generated descriptor for an XML attribute. */
typedef struct {
	const char *name;                 /**< Local attribute name. */
	zxsd_type_t type;                 /**< Primitive value kind. */
	int required;                     /**< Non-zero when required. */
	const zxsd_simple_t *simple_type; /**< Simple restriction, if any. */
	const char *namespace_uri;        /**< Required namespace, or @c NULL. */
	const char *default_value;        /**< Value used when absent, or @c NULL. */
	const char *fixed_value;          /**< Required value when present. */
} zxsd_attribute_t;

/** XML Schema element or attribute wildcard. */
typedef struct {
	const char *namespace_constraint; /**< Original namespace constraint. */
	const char *target_namespace;     /**< Schema target namespace, or @c NULL. */
	zxsd_process_t process_contents;  /**< strict, lax or skip behavior. */
} zxsd_wildcard_t;

/** Recursive descriptor for a compiled content-model particle. */
typedef struct zxsd_particle {
	zxsd_particle_kind_t kind;            /**< Particle kind. */
	unsigned long min_occurs;             /**< Minimum repetitions. */
	unsigned long max_occurs;             /**< Maximum, or @c ULONG_MAX. */
	const zxsd_element_t *element;        /**< Element for element particles. */
	const struct zxsd_particle *children; /**< Group child particles. */
	size_t child_count;                   /**< Number of group children. */
	const zxsd_wildcard_t *wildcard;      /**< Wildcard configuration. */
} zxsd_particle_t;

/** Generated descriptor for a complex XML Schema type. */
typedef struct zxsd_complex {
	const zxsd_element_t *elements;             /**< Child particle descriptors. */
	size_t element_count;                       /**< Number of child descriptors. */
	const zxsd_attribute_t *attributes;         /**< Attribute descriptors. */
	size_t attribute_count;                     /**< Number of attribute descriptors. */
	zxsd_model_t model;                         /**< Child content model. */
	unsigned long min_occurs;                   /**< Minimum model occurrences. */
	unsigned long max_occurs;                   /**< Maximum model occurrences. */
	const zxsd_simple_t *simple_content;        /**< Text type for simpleContent. */
	zxsd_type_t simple_content_type;            /**< Built-in simpleContent kind. */
	int has_simple_content;                     /**< Non-zero for simpleContent. */
	int abstract_type;                          /**< Non-zero if direct use is forbidden. */
	const zxsd_particle_t *particle;            /**< Recursive v0.7 content model. */
	const zxsd_wildcard_t *any_attribute;       /**< Attribute wildcard, or @c NULL. */
	const struct zxsd_complex *base_definition; /**< Named base complex type. */
	unsigned derivation_method;                 /**< Extension or restriction. */
	unsigned final_methods;                     /**< Methods forbidden to descendants. */
	const char *type_name;                      /**< Named type local name, or @c NULL. */
	const char *type_namespace;                 /**< Named type namespace, or @c NULL. */
	int mixed_content;                          /**< Non-zero when character data is allowed. */
	int any_type;                               /**< Non-zero for the built-in xs:anyType. */
	unsigned block_methods; /**< Derivations forbidden when this type is used through xsi:type.
				 */
} zxsd_complex_t;

/** One expanded-name step in the restricted identity XPath subset. */
typedef struct {
	const char *name;          /**< Local name, or @c NULL for a wildcard step. */
	const char *namespace_uri; /**< Required namespace, or @c NULL. */
} zxsd_xpath_step_t;

/** Compiled restricted XPath used by identity constraints. */
typedef struct zxsd_xpath {
	const zxsd_xpath_step_t *steps;        /**< Path steps. */
	size_t step_count;                     /**< Number of steps. */
	int descendant;                        /**< Non-zero for a leading @c .//. */
	int attribute;                         /**< Non-zero when the final step is an attribute. */
	const struct zxsd_xpath *alternatives; /**< Union branches, or @c NULL. */
	size_t alternative_count;              /**< Number of union branches. */
} zxsd_xpath_t;

/** Identity-constraint category. */
typedef enum {
	ZXSD_ID_UNIQUE = 0, /**< xs:unique. */
	ZXSD_ID_KEY,        /**< xs:key. */
	ZXSD_ID_KEYREF      /**< xs:keyref. */
} zxsd_identity_kind_t;

/** Compiled xs:unique, xs:key or xs:keyref descriptor. */
typedef struct {
	const char *name;           /**< Constraint name. */
	zxsd_identity_kind_t kind;  /**< Constraint category. */
	zxsd_xpath_t scope;         /**< Element path that owns each constraint scope. */
	zxsd_xpath_t selector;      /**< Selected element path. */
	const zxsd_xpath_t *fields; /**< Tuple fields. */
	size_t field_count;         /**< Number of tuple fields. */
	size_t refer_index;         /**< Referenced key index for keyref. */
} zxsd_identity_t;

/** Global element declaration available to strict/lax wildcards. */
typedef struct {
	const char *name;                        /**< Local element name. */
	const char *namespace_uri;               /**< Namespace URI, or @c NULL. */
	zxsd_type_t type;                        /**< Value kind. */
	const struct zxsd_complex *complex_type; /**< Complex descriptor, if any. */
	const zxsd_simple_t *simple_type;        /**< Simple descriptor, if any. */
	const char *default_value;               /**< Schema default, or @c NULL. */
	const char *fixed_value;                 /**< Required fixed value, or @c NULL. */
	int nillable;                            /**< Non-zero when xsi:nil is allowed. */
	int abstract_element;                    /**< Non-zero for an abstract declaration. */
	unsigned block_methods;                  /**< Prohibited substitutions. */
	const char *substitution_head_name;      /**< Head local name, or @c NULL. */
	const char *substitution_head_namespace; /**< Head namespace, or @c NULL. */
} zxsd_global_element_t;

/** Global attribute declaration available to strict/lax wildcards. */
typedef struct {
	const char *name;                 /**< Local attribute name. */
	const char *namespace_uri;        /**< Namespace URI, or @c NULL. */
	zxsd_type_t type;                 /**< Simple value kind. */
	const zxsd_simple_t *simple_type; /**< Restricted type, or @c NULL. */
	const char *default_value;        /**< Schema default, or @c NULL. */
	const char *fixed_value;          /**< Required fixed value, or @c NULL. */
} zxsd_global_attribute_t;

/** Named type entry used to resolve runtime xsi:type values. */
typedef struct {
	const char *name;                        /**< Type local name. */
	const char *namespace_uri;               /**< Type namespace, or @c NULL. */
	zxsd_type_t type;                        /**< Value kind. */
	const struct zxsd_complex *complex_type; /**< Complex descriptor. */
	const zxsd_simple_t *simple_type;        /**< Simple descriptor. */
} zxsd_named_type_t;

/** Global XML Schema notation declaration. */
typedef struct {
	const char *name;              /**< Notation local name. */
	const char *namespace_uri;     /**< Target namespace URI, or @c NULL. */
	const char *public_identifier; /**< Public identifier, or @c NULL. */
	const char *system_identifier; /**< System URI, or @c NULL. */
} zxsd_notation_t;

/** Root descriptor consumed by the generic validation runtime. */
typedef struct {
	const char *root_name;                            /**< Root local name. */
	zxsd_type_t root_type;                            /**< Root value kind. */
	const zxsd_complex_t *root_complex;               /**< Root complex descriptor. */
	const zxsd_simple_t *root_simple;                 /**< Root simple restriction. */
	const char *root_namespace_uri;                   /**< Root namespace, or @c NULL. */
	const zxsd_identity_t *identities;                /**< Root identity constraints. */
	size_t identity_count;                            /**< Number of identity constraints. */
	const zxsd_global_element_t *global_elements;     /**< Wildcard declaration registry. */
	size_t global_element_count;                      /**< Number of global declarations. */
	const zxsd_global_attribute_t *global_attributes; /**< Attribute registry. */
	size_t global_attribute_count;                    /**< Number of global attributes. */
	const zxsd_named_type_t *named_types;             /**< Runtime xsi:type registry. */
	size_t named_type_count;                          /**< Number of named type entries. */
	const char *default_value;                        /**< Root default, or @c NULL. */
	const char *fixed_value;                          /**< Root fixed value, or @c NULL. */
	int nillable;                     /**< Non-zero when root permits xsi:nil. */
	int abstract_element;             /**< Non-zero when root is abstract. */
	unsigned block_methods;           /**< Root block constraints. */
	const zxsd_notation_t *notations; /**< Global notation declaration registry. */
	size_t notation_count;            /**< Number of notation declarations. */
} zxsd_schema_t;

/** Internal immutable continuation or frontier entry, for static allocation only. */
typedef struct {
	const struct zxsd_particle *particle; /**< Particle interpreted by this continuation. */
	unsigned long count;                  /**< Completed non-empty occurrences. */
	size_t index;                         /**< Next sequence term. */
	uint32_t references;                  /**< Owning references to this shared entry. */
	uint16_t parent;                      /**< Continuation after this particle, or zero. */
	uint16_t next;                        /**< Frontier/free-list link, or zero. */
	unsigned char phase;                  /**< Internal interpreter instruction. */
	unsigned char started;                /**< Current group iteration consumed a token. */
} zxsd_stream_model_node_t;

/** Internal per-element state exposed only to permit static allocation. */
typedef struct {
	const char *expected_name;          /**< Schema-owned expected local name. */
	const char *expected_namespace;     /**< Schema-owned expected namespace. */
	zxsd_type_t type;                   /**< Effective value type. */
	const zxsd_complex_t *complex_type; /**< Effective complex type. */
	const zxsd_simple_t *simple_type;   /**< Effective simple type. */
	const char *default_value;          /**< Element default. */
	const char *fixed_value;            /**< Element fixed value. */
	unsigned char seen_attributes[ZXSD_MAX_ATTRIBUTES]; /**< Seen attributes. */
	unsigned long child_counts[ZXSD_MAX_PARTICLES];     /**< Consumed child counts. */
	size_t sequence_index;                              /**< Current sequence position. */
	int incremental_model; /**< Internal content-model cursor mode, or zero for history. */
	uint16_t model_states; /**< Head of the nested-model frontier in the shared pool. */
	unsigned long
		model_occurrences; /**< Non-empty sequence iterations consumed by the cursor. */
	/** Distinct leaf particles referenced by the compact child-token history. */
	const struct zxsd_particle *child_particle_table[ZXSD_MAX_PARTICLES];
	size_t child_particle_table_count; /**< Number of entries in the particle table. */
	/** 16-bit particle-table index for each child in recursive content models. */
	uint16_t child_particle_ids[ZXSD_STREAM_CHILD_CAPACITY];
	size_t text_length;                         /**< Bytes currently in @ref text. */
	size_t child_count;                         /**< Number of child events received. */
	int nillable;                               /**< Whether xsi:nil is permitted. */
	int nilled;                                 /**< Whether xsi:nil was true. */
	int content_started;                        /**< Whether attributes are closed. */
	int skip_validation;                        /**< Wildcard skip/lax unknown subtree. */
	size_t namespace_base;                      /**< Namespace stack base for this element. */
	char local_name[ZXSD_STREAM_NAME_CAPACITY]; /**< Actual local name. */
	char namespace_uri[ZXSD_STREAM_NAME_CAPACITY]; /**< Actual namespace URI. */
	char text[ZXSD_STREAM_TEXT_CAPACITY + 1u];     /**< Decoded character data. */
} zxsd_stream_frame_t;

/** Internal namespace binding retained by the event-driven validator. */
typedef struct {
	char prefix[ZXSD_STREAM_NAME_CAPACITY]; /**< Lexical prefix, or an empty string. */
	char uri[ZXSD_STREAM_NAME_CAPACITY];    /**< Bound namespace URI. */
} zxsd_stream_namespace_t;

/**
 * @brief Bounded state for the event-driven validator.
 *
 * Initialize this object with @ref zxsd_stream_init before sending events.
 * Its storage is bounded by compile-time configuration constants. Identity
 * constraints additionally use the configured allocator.
 * Flat content models use counters. Nested sequences and choices use a shared,
 * bounded continuation pool, including alternative repetition partitions.
 * Zero @ref ZXSD_STREAM_MODEL_CAPACITY selects the bounded history fallback.
 */
typedef struct {
	const zxsd_schema_t *schema;         /**< Generated schema being validated. */
	zxsd_result_t result;                /**< Sticky result of the first failed event. */
	size_t depth;                        /**< Number of currently open elements. */
	size_t event_offset;                 /**< Monotonic event index used in diagnostics. */
	int finished;                        /**< Non-zero after a complete root element. */
	zxsd_allocator_t identity_allocator; /**< Allocator for streamed identity validation. */
	char *identity_xml;                  /**< Internal reconstructed XML buffer. */
	size_t identity_xml_length;          /**< Bytes used in @ref identity_xml. */
	size_t identity_xml_capacity;        /**< Allocated reconstructed XML bytes. */
	zxsd_stream_namespace_t namespaces[ZXSD_MAX_NAMESPACES]; /**< Active QName bindings. */
	size_t namespace_count; /**< Number of active entries in @ref namespaces. */
#if ZXSD_STREAM_MODEL_CAPACITY > 0
	/** Shared continuation/frontier storage; independent of document length. */
	zxsd_stream_model_node_t model_nodes[ZXSD_STREAM_MODEL_CAPACITY];
	uint16_t model_free;        /**< First free pool slot, or zero. */
	uint16_t model_high_water;  /**< Highest slot ever allocated; bounds interning scans. */
	int model_pool_initialized; /**< Whether the lazy free list has been initialized. */
#endif
	zxsd_stream_frame_t frames[ZXSD_MAX_DEPTH]; /**< Fixed element stack. */
} zxsd_stream_t;

/** Decoded XML events exposed by the incremental raw-XML reader. */
typedef enum {
	ZXSD_XML_EVENT_START_ELEMENT, /**< Start element with expanded name. */
	ZXSD_XML_EVENT_ATTRIBUTE,     /**< Attribute on the current element. */
	ZXSD_XML_EVENT_TEXT,          /**< Decoded character data. */
	ZXSD_XML_EVENT_END_ELEMENT    /**< End element with expanded name. */
} zxsd_xml_event_t;

/**
 * @brief Observe one decoded event accepted by the incremental validator.
 *
 * All strings are transient and valid only for the duration of the callback.
 * @p value is set only for attribute and text events. Text can contain embedded
 * NUL bytes and must be consumed using @p value_length.
 *
 * @param[in] user_data Opaque value supplied by the application.
 * @param[in] event Event category.
 * @param[in] namespace_uri Namespace URI, or an empty string.
 * @param[in] local_name Local element or attribute name, or @c NULL for text.
 * @param[in] value Attribute value or character data, otherwise @c NULL.
 * @param[in] value_length Number of bytes in @p value.
 */
typedef void (*zxsd_xml_event_fn)(void *user_data, zxsd_xml_event_t event,
				  const char *namespace_uri, const char *local_name,
				  const char *value, size_t value_length);

/**
 * @brief Bounded raw-XML reader feeding an event-driven validator.
 *
 * The object accepts arbitrary byte fragments and retains only an incomplete
 * lexical token between calls. Initialize it with @ref zxsd_xml_reader_init,
 * then call @ref zxsd_xml_reader_feed and @ref zxsd_xml_reader_finish.
 */
typedef struct {
	zxsd_stream_t validator; /**< Event-driven validator receiving decoded XML events. */
	zxsd_result_t result;    /**< Sticky result of the first parser or validator error. */
	size_t offset;           /**< Total number of input bytes accepted. */
	size_t token_offset;     /**< Absolute offset of the retained token. */
	size_t token_length;     /**< Bytes currently retained in @ref token. */
	unsigned char markup;    /**< Non-zero while retaining markup beginning with @c <. */
	unsigned char quote;     /**< Active attribute quote, or zero. */
	zxsd_xml_event_fn event_callback;         /**< Optional decoded-event observer. */
	void *event_user_data;                    /**< Opaque observer context. */
	char token[ZXSD_XML_TOKEN_CAPACITY + 1u]; /**< Incomplete text or markup token. */
	char decoded[ZXSD_MAX_TEXT_LENGTH + 1u];  /**< Decoded text or attribute scratch. */
} zxsd_xml_reader_t;

/** @cond INTERNAL */
#ifdef ZXSD_NO_HEAP
#define ZXSD_CONFIG_NO_HEAP 1u
#else
#define ZXSD_CONFIG_NO_HEAP 0u
#endif
/** @endcond */

/**
 * @brief Caller configuration stamp for a same-toolchain runtime boundary.
 *
 * Initialize with @ref ZXSD_RUNTIME_CONFIG_INIT in application code. The fixed
 * array order is part of @ref ZXSD_RUNTIME_ABI_VERSION. This stamp is not a
 * portable wire format and does not replace matching architecture/ABI flags.
 */
typedef struct {
	unsigned int runtime_abi;    /**< Public API/layout epoch. */
	unsigned int descriptor_abi; /**< Descriptor interpretation epoch. */
	unsigned int no_heap;        /**< Whether @c ZXSD_NO_HEAP was defined. */
	size_t limits[22];           /**< Exact public resource limits, in declaration order. */
	size_t layouts[12];          /**< Public storage and fundamental type sizes. */
} zxsd_runtime_config_t;

/** Initialize a runtime stamp using the including translation unit's configuration. */
#define ZXSD_RUNTIME_CONFIG_INIT                                                                   \
	{                                                                                          \
		ZXSD_RUNTIME_ABI_VERSION, ZXSD_DESCRIPTOR_ABI_VERSION, ZXSD_CONFIG_NO_HEAP,        \
			{ZXSD_MAX_INPUT,                                                           \
			 ZXSD_MAX_DEPTH,                                                           \
			 ZXSD_MAX_PARTICLES,                                                       \
			 ZXSD_MAX_ATTRIBUTES,                                                      \
			 ZXSD_MAX_NAMESPACES,                                                      \
			 ZXSD_MAX_IDENTITY_ROWS,                                                   \
			 ZXSD_MAX_IDENTITY_VALUES,                                                 \
			 ZXSD_MAX_IDENTITY_TEXT,                                                   \
			 ZXSD_MAX_IDENTITY_CONSTRAINTS,                                            \
			 ZXSD_MAX_IDENTITY_FIELDS,                                                 \
			 ZXSD_MAX_XPATH_DEPTH,                                                     \
			 ZXSD_MAX_XPATH_ALTERNATIVES,                                              \
			 ZXSD_STREAM_TEXT_CAPACITY,                                                \
			 ZXSD_MAX_TEXT_LENGTH,                                                     \
			 ZXSD_MAX_PATTERN_LENGTH,                                                  \
			 ZXSD_MAX_REGEX_NODES,                                                     \
			 ZXSD_MAX_REGEX_DEPTH,                                                     \
			 ZXSD_MAX_SCHEMA_COMPLEXITY,                                               \
			 ZXSD_STREAM_NAME_CAPACITY,                                                \
			 ZXSD_STREAM_CHILD_CAPACITY,                                               \
			 ZXSD_STREAM_MODEL_CAPACITY,                                               \
			 ZXSD_XML_TOKEN_CAPACITY},                                                 \
		{                                                                                  \
			sizeof(zxsd_stream_t), sizeof(zxsd_stream_frame_t),                        \
				sizeof(zxsd_xml_reader_t), sizeof(zxsd_schema_t),                  \
				sizeof(zxsd_particle_t), sizeof(zxsd_complex_t),                   \
				sizeof(zxsd_simple_t), sizeof(zxsd_result_t),                      \
				sizeof(zxsd_allocator_t), sizeof(void *), sizeof(size_t),          \
				sizeof(unsigned long)                                              \
		}                                                                                  \
	}

/**
 * @brief Check application limits and layouts before using runtime-owned code.
 *
 * Call once before validation or initializing caller-owned contexts when linking
 * a separately built runtime. No context or descriptor is accessed. Matching
 * stamps are necessary, not sufficient, for binary compatibility: architecture,
 * calling convention, packing, and compiler ABI options must also match.
 *
 * @param[in] expected Application stamp initialized with @ref ZXSD_RUNTIME_CONFIG_INIT.
 * @retval ZXSD_OK Epochs, resource limits, and recorded layout sizes match.
 * @retval ZXSD_ERR_ARGUMENT The stamp is @c NULL.
 * @retval ZXSD_ERR_INCOMPATIBLE_ABI The runtime configuration differs.
 * @return A structured result.
 */
zxsd_result_t zxsd_check_runtime(const zxsd_runtime_config_t *expected);

/**
 * @brief Validate an XML document against a generated schema descriptor.
 *
 * @param[in] schema Generated schema descriptor.
 * @param[in] xml NUL-terminated XML document.
 *
 * @retval ZXSD_OK The document is valid.
 * @retval ZXSD_ERR_ARGUMENT An argument is invalid.
 * @return A structured validation result for all other outcomes.
 */
zxsd_result_t zxsd_validate(const zxsd_schema_t *schema, const char *xml);

/**
 * @brief Validate using an application-provided scratch allocator.
 *
 * Pass @c NULL as @p allocator to select zero-heap operation. Schemas with
 * identity constraints then return @ref ZXSD_ERR_RESOURCE_LIMIT.
 *
 * @param[in] schema Generated schema descriptor.
 * @param[in] xml NUL-terminated XML document.
 * @param[in] allocator Scratch allocator, or @c NULL.
 *
 * @retval ZXSD_OK The document is valid.
 * @retval ZXSD_ERR_ARGUMENT An argument is invalid.
 * @retval ZXSD_ERR_RESOURCE_LIMIT Scratch storage is required but unavailable.
 * @return A structured validation result for all other outcomes.
 */
zxsd_result_t zxsd_validate_with_allocator(const zxsd_schema_t *schema, const char *xml,
					   const zxsd_allocator_t *allocator);

/**
 * @brief Initialize an event-driven validation context.
 *
 * @param[out] stream Validation context to initialize.
 * @param[in] schema Generated schema descriptor.
 *
 * @retval ZXSD_OK The context was initialized.
 * @retval ZXSD_ERR_ARGUMENT An argument is invalid.
 * @retval ZXSD_ERR_RESOURCE_LIMIT The schema requires explicit scratch storage.
 */
zxsd_result_t zxsd_stream_init(zxsd_stream_t *stream, const zxsd_schema_t *schema);

/**
 * @brief Initialize streaming validation with identity-constraint scratch allocation.
 *
 * The allocator is used only when the schema contains identity constraints. Passing
 * @c NULL is valid for schemas without them and otherwise returns
 * @ref ZXSD_ERR_RESOURCE_LIMIT.
 *
 * @param[out] stream Validation context to initialize.
 * @param[in] schema Generated schema descriptor.
 * @param[in] allocator Scratch allocator, or @c NULL.
 *
 * @retval ZXSD_OK The context was initialized.
 * @retval ZXSD_ERR_ARGUMENT An argument is invalid.
 * @retval ZXSD_ERR_RESOURCE_LIMIT Scratch storage could not be allocated.
 */
zxsd_result_t zxsd_stream_init_with_allocator(zxsd_stream_t *stream, const zxsd_schema_t *schema,
					      const zxsd_allocator_t *allocator);

/**
 * @brief Release optional streaming scratch storage.
 *
 * Call this after an application aborts an event sequence. Calling it on an
 * initialized context that owns no scratch storage is safe.
 *
 * @param[in,out] stream Validation context to dispose.
 */
void zxsd_stream_dispose(zxsd_stream_t *stream);

/**
 * @brief Begin an element identified by its expanded name.
 *
 * @param[in,out] stream Initialized validation context.
 * @param[in] namespace_uri Namespace URI, or @c NULL for no namespace.
 * @param[in] local_name Element local name.
 *
 * @return The sticky validation result.
 */
zxsd_result_t zxsd_start_element(zxsd_stream_t *stream, const char *namespace_uri,
				 const char *local_name);

/**
 * @brief Add an in-scope namespace declaration to the current start tag.
 *
 * Call this for bindings used by QName- or NOTATION-valued attributes/text.
 *
 * @param[in,out] stream Initialized validation context.
 * @param[in] prefix Namespace prefix, or an empty string for the default namespace.
 * @param[in] namespace_uri Namespace URI bound to @p prefix.
 *
 * @return The sticky validation result.
 */
zxsd_result_t zxsd_namespace(zxsd_stream_t *stream, const char *prefix, const char *namespace_uri);

/**
 * @brief Add an attribute to the current start tag using its expanded name.
 *
 * @param[in,out] stream Initialized validation context.
 * @param[in] namespace_uri Namespace URI, or @c NULL for no namespace.
 * @param[in] local_name Attribute local name.
 * @param[in] value Decoded attribute value.
 *
 * @return The sticky validation result.
 */
zxsd_result_t zxsd_attribute(zxsd_stream_t *stream, const char *namespace_uri,
			     const char *local_name, const char *value);

/**
 * @brief Apply a namespace-resolved @c xsi:type to the current element.
 *
 * @param[in,out] stream Initialized validation context.
 * @param[in] type_namespace_uri Type namespace URI, or @c NULL for no namespace.
 * @param[in] type_local_name Type local name.
 *
 * @return The sticky validation result.
 */
zxsd_result_t zxsd_xsi_type(zxsd_stream_t *stream, const char *type_namespace_uri,
			    const char *type_local_name);

/**
 * @brief Close the attribute phase of the current start tag.
 *
 * @param[in,out] stream Initialized validation context.
 *
 * @return The sticky validation result.
 */
zxsd_result_t zxsd_start_content(zxsd_stream_t *stream);

/**
 * @brief Supply decoded character data for the current element.
 *
 * @param[in,out] stream Initialized validation context.
 * @param[in] text Decoded character data; it need not be NUL-terminated.
 * @param[in] length Number of bytes in @p text.
 *
 * @return The sticky validation result.
 */
zxsd_result_t zxsd_text(zxsd_stream_t *stream, const char *text, size_t length);

/**
 * @brief End the current element and check its expanded name and content.
 *
 * @param[in,out] stream Initialized validation context.
 * @param[in] namespace_uri Namespace URI, or @c NULL for no namespace.
 * @param[in] local_name Element local name.
 *
 * @return The sticky validation result.
 */
zxsd_result_t zxsd_end_element(zxsd_stream_t *stream, const char *namespace_uri,
			       const char *local_name);

/**
 * @brief Verify that exactly one complete root element was supplied.
 *
 * @param[in,out] stream Initialized validation context.
 *
 * @return The final sticky validation result.
 */
zxsd_result_t zxsd_stream_finish(zxsd_stream_t *stream);

/**
 * @brief Initialize a raw, incrementally fed XML validator.
 *
 * @param[out] reader Reader context to initialize.
 * @param[in] schema Generated schema descriptor.
 *
 * @return A structured initialization result.
 */
zxsd_result_t zxsd_xml_reader_init(zxsd_xml_reader_t *reader, const zxsd_schema_t *schema);

/**
 * @brief Initialize a raw XML reader with identity-constraint scratch allocation.
 *
 * @param[out] reader Reader context to initialize.
 * @param[in] schema Generated schema descriptor.
 * @param[in] allocator Scratch allocator, or @c NULL.
 *
 * @return A structured initialization result.
 */
zxsd_result_t zxsd_xml_reader_init_with_allocator(zxsd_xml_reader_t *reader,
						  const zxsd_schema_t *schema,
						  const zxsd_allocator_t *allocator);

/**
 * @brief Install an optional decoded-event observer.
 *
 * The observer runs synchronously after the validator accepts each event. It
 * may copy selected values but must not retain the supplied string pointers.
 *
 * @param[in,out] reader Initialized reader.
 * @param[in] callback Observer callback, or @c NULL to disable observation.
 * @param[in] user_data Opaque value passed to @p callback.
 */
void zxsd_xml_reader_set_event_callback(zxsd_xml_reader_t *reader, zxsd_xml_event_fn callback,
					void *user_data);

/**
 * @brief Feed one arbitrary fragment of an XML document.
 *
 * Fragments need not be NUL-terminated and may end inside UTF-8 sequences,
 * entity references, tags, attributes, comments, processing instructions, or
 * CDATA sections. Input storage may be reused as soon as this function returns.
 *
 * @param[in,out] reader Initialized reader.
 * @param[in] data Input bytes; may be @c NULL only when @p length is zero.
 * @param[in] length Number of bytes in @p data.
 *
 * @return The sticky parsing or validation result.
 */
zxsd_result_t zxsd_xml_reader_feed(zxsd_xml_reader_t *reader, const void *data, size_t length);

/**
 * @brief Finish an incrementally supplied XML document.
 *
 * @param[in,out] reader Initialized reader.
 *
 * @retval ZXSD_OK The complete document is well formed and schema-valid.
 * @retval ZXSD_ERR_MALFORMED_XML The document ends in an incomplete token.
 * @return The sticky parsing or validation result.
 */
zxsd_result_t zxsd_xml_reader_finish(zxsd_xml_reader_t *reader);

/**
 * @brief Release optional scratch storage owned by a raw XML reader.
 *
 * @param[in,out] reader Initialized reader to dispose.
 */
void zxsd_xml_reader_dispose(zxsd_xml_reader_t *reader);

/**
 * @brief Get a stable human-readable description of an error code.
 *
 * @param[in] error Error code to describe.
 *
 * @return A pointer to a static NUL-terminated string.
 */
const char *zxsd_error_string(zxsd_error_t error);

/** @} */

#ifdef __cplusplus
}
#endif
#endif
