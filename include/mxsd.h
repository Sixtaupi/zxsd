/*
 * Copyright (c) 2026 Romain Pelletant
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Public API for the mxsd XML validation runtime.
 */

#ifndef MXSD_H
#define MXSD_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup mxsd_api mxsd validation API
 * @{
 */

#ifndef MXSD_MAX_INPUT
/** Maximum accepted XML input size, in bytes. */
#define MXSD_MAX_INPUT (1024u * 1024u)
#endif
#ifndef MXSD_MAX_DEPTH
/** Maximum nesting depth accepted by the embedded XML parser. */
#define MXSD_MAX_DEPTH 32u
#endif
#ifndef MXSD_MAX_PARTICLES
/** Maximum number of child particles in one complex type. */
#define MXSD_MAX_PARTICLES 32u
#endif
#ifndef MXSD_MAX_ATTRIBUTES
/** Maximum number of attributes in one element. */
#define MXSD_MAX_ATTRIBUTES 32u
#endif
#ifndef MXSD_MAX_NAMESPACES
/** Maximum number of simultaneously active namespace bindings. */
#define MXSD_MAX_NAMESPACES 16u
#endif
#ifndef MXSD_MAX_IDENTITY_ROWS
/** Maximum stored tuples per identity constraint. */
#define MXSD_MAX_IDENTITY_ROWS 64u
#endif
#ifndef MXSD_MAX_IDENTITY_VALUES
/** Maximum identity values, retained as a compatibility alias for rows. */
#define MXSD_MAX_IDENTITY_VALUES MXSD_MAX_IDENTITY_ROWS
#endif
#ifndef MXSD_MAX_IDENTITY_CONSTRAINTS
/** Maximum identity constraints attached to one root declaration. */
#define MXSD_MAX_IDENTITY_CONSTRAINTS 16u
#endif
#ifndef MXSD_MAX_IDENTITY_FIELDS
/** Maximum fields in one identity-constraint tuple. */
#define MXSD_MAX_IDENTITY_FIELDS 8u
#endif
#ifndef MXSD_MAX_XPATH_DEPTH
/** Maximum number of steps in a compiled identity XPath. */
#define MXSD_MAX_XPATH_DEPTH 8u
#endif
#ifndef MXSD_STREAM_TEXT_CAPACITY
/** Maximum decoded character data retained for one streamed element. */
#define MXSD_STREAM_TEXT_CAPACITY MXSD_MAX_TEXT_LENGTH
#endif
#ifndef MXSD_MAX_TEXT_LENGTH
/** Maximum lexical length of one element or attribute value. */
#define MXSD_MAX_TEXT_LENGTH 1024u
#endif
#ifndef MXSD_MAX_SCHEMA_COMPLEXITY
/** Maximum aggregate descriptor count accepted by the runtime. */
#define MXSD_MAX_SCHEMA_COMPLEXITY 4096u
#endif
#ifndef MXSD_STREAM_NAME_CAPACITY
/** Maximum local-name or namespace-URI length accepted by streaming calls. */
#define MXSD_STREAM_NAME_CAPACITY 128u
#endif

/** Validation status and error codes. */
typedef enum {
	MXSD_OK = 0,                   /**< Validation succeeded. */
	MXSD_ERR_ARGUMENT,             /**< A required argument is invalid. */
	MXSD_ERR_INPUT_TOO_LARGE,      /**< XML exceeds @ref MXSD_MAX_INPUT. */
	MXSD_ERR_MALFORMED_XML,        /**< XML syntax is malformed. */
	MXSD_ERR_EXCESSIVE_NESTING,    /**< XML nesting exceeds the limit. */
	MXSD_ERR_WRONG_ROOT,           /**< The document root does not match. */
	MXSD_ERR_MISSING_ELEMENT,      /**< A required child element is absent. */
	MXSD_ERR_UNEXPECTED_ELEMENT,   /**< A child element is not permitted. */
	MXSD_ERR_ELEMENT_ORDER,        /**< Sequence order is incorrect. */
	MXSD_ERR_OCCURRENCE,           /**< An occurrence bound was violated. */
	MXSD_ERR_MISSING_ATTRIBUTE,    /**< A required attribute is absent. */
	MXSD_ERR_UNEXPECTED_ATTRIBUTE, /**< An attribute is not permitted. */
	MXSD_ERR_INVALID_VALUE,        /**< A primitive or restricted value is invalid. */
	MXSD_ERR_TEXT_NOT_ALLOWED,     /**< Text occurred in element-only content. */
	MXSD_ERR_IDENTITY_CONSTRAINT,  /**< A unique, key or keyref rule failed. */
	MXSD_ERR_STREAM_LIMIT,         /**< A fixed streaming buffer was exceeded. */
	MXSD_ERR_RESOURCE_LIMIT        /**< A configured resource limit was exceeded. */
} mxsd_error_t;

/** Structured result returned by every generated validator. */
typedef struct {
	mxsd_error_t code;   /**< Validation status. */
	size_t offset;       /**< Byte offset associated with the result. */
	const char *path;    /**< Schema item involved, or @c NULL. */
	const char *message; /**< Static, human-readable status text. */
} mxsd_result_t;

/** Allocate runtime scratch storage. */
typedef void *(*mxsd_allocate_fn)(void *context, size_t size);

/** Release runtime scratch storage. */
typedef void (*mxsd_deallocate_fn)(void *context, void *pointer);

/** Optional allocator used by validation features requiring scratch storage. */
typedef struct {
	mxsd_allocate_fn allocate;     /**< Allocation callback. */
	mxsd_deallocate_fn deallocate; /**< Matching release callback. */
	void *context;                 /**< Opaque callback context. */
} mxsd_allocator_t;

/** Primitive and compound value kinds understood by the runtime. */
typedef enum {
	MXSD_T_STRING,               /**< XML Schema string. */
	MXSD_T_BOOLEAN,              /**< XML Schema boolean. */
	MXSD_T_INT,                  /**< Signed 32-bit XML Schema integer. */
	MXSD_T_UNSIGNED_INT,         /**< Unsigned 32-bit XML Schema integer. */
	MXSD_T_INTEGER,              /**< Arbitrary-width integer lexical form. */
	MXSD_T_DECIMAL,              /**< XML Schema decimal. */
	MXSD_T_FLOAT,                /**< XML Schema float. */
	MXSD_T_DOUBLE,               /**< XML Schema double. */
	MXSD_T_LONG,                 /**< Signed 64-bit XML Schema integer. */
	MXSD_T_SHORT,                /**< Signed 16-bit XML Schema integer. */
	MXSD_T_BYTE,                 /**< Signed 8-bit XML Schema integer. */
	MXSD_T_NON_POSITIVE_INTEGER, /**< Integer less than or equal to zero. */
	MXSD_T_NEGATIVE_INTEGER,     /**< Integer less than zero. */
	MXSD_T_NON_NEGATIVE_INTEGER, /**< Integer greater than or equal to zero. */
	MXSD_T_POSITIVE_INTEGER,     /**< Integer greater than zero. */
	MXSD_T_UNSIGNED_LONG,        /**< Unsigned 64-bit XML Schema integer. */
	MXSD_T_UNSIGNED_SHORT,       /**< Unsigned 16-bit XML Schema integer. */
	MXSD_T_UNSIGNED_BYTE,        /**< Unsigned 8-bit XML Schema integer. */
	MXSD_T_NORMALIZED_STRING,    /**< XML Schema normalizedString. */
	MXSD_T_TOKEN,                /**< XML Schema token. */
	MXSD_T_LANGUAGE,             /**< XML Schema language. */
	MXSD_T_NAME,                 /**< XML Schema Name. */
	MXSD_T_NCNAME,               /**< XML Schema NCName. */
	MXSD_T_NMTOKEN,              /**< XML Schema NMTOKEN. */
	MXSD_T_QNAME,                /**< XML Schema QName. */
	MXSD_T_ANY_URI,              /**< XML Schema anyURI. */
	MXSD_T_DURATION,             /**< XML Schema duration. */
	MXSD_T_DATE,                 /**< XML Schema date. */
	MXSD_T_DATE_TIME,            /**< XML Schema dateTime. */
	MXSD_T_TIME,                 /**< XML Schema time. */
	MXSD_T_GYEAR,                /**< XML Schema gYear. */
	MXSD_T_GYEAR_MONTH,          /**< XML Schema gYearMonth. */
	MXSD_T_GMONTH,               /**< XML Schema gMonth. */
	MXSD_T_GMONTH_DAY,           /**< XML Schema gMonthDay. */
	MXSD_T_GDAY,                 /**< XML Schema gDay. */
	MXSD_T_HEX_BINARY,           /**< XML Schema hexBinary. */
	MXSD_T_BASE64_BINARY,        /**< XML Schema base64Binary. */
	MXSD_T_ID,                   /**< XML Schema ID. */
	MXSD_T_IDREF,                /**< XML Schema IDREF. */
	MXSD_T_ENTITY,               /**< XML Schema ENTITY. */
	MXSD_T_NOTATION,             /**< XML Schema NOTATION QName. */
	MXSD_T_COMPLEX               /**< Element-only complex content. */
} mxsd_type_t;

/** Whitespace treatment prescribed by an XML Schema simple type. */
typedef enum {
	MXSD_WS_PRESERVE = 0, /**< Keep every character unchanged. */
	MXSD_WS_REPLACE,      /**< Replace tab, carriage return and line feed. */
	MXSD_WS_COLLAPSE      /**< Replace, trim and collapse whitespace runs. */
} mxsd_whitespace_t;

/** Variety of an XML Schema simple type. */
typedef enum {
	MXSD_SIMPLE_ATOMIC = 0, /**< One atomic value. */
	MXSD_SIMPLE_LIST,       /**< Whitespace-separated item sequence. */
	MXSD_SIMPLE_UNION       /**< Value accepted by at least one member type. */
} mxsd_simple_variety_t;

/** Complex content model kind. */
typedef enum {
	MXSD_SEQUENCE = 0, /**< Children occur in declaration order. */
	MXSD_CHOICE,       /**< Children select from alternatives. */
	MXSD_ALL           /**< Children may occur in any order. */
} mxsd_model_t;

/** Wildcard validation behavior. */
typedef enum {
	MXSD_PROCESS_STRICT = 0, /**< A matching declaration is required. */
	MXSD_PROCESS_LAX,        /**< Validate when a declaration is available. */
	MXSD_PROCESS_SKIP        /**< Accept the matching subtree without validation. */
} mxsd_process_t;

/** Compiled content-model particle kind. */
typedef enum {
	MXSD_PARTICLE_ELEMENT = 0, /**< Element declaration. */
	MXSD_PARTICLE_SEQUENCE,    /**< Ordered child particles. */
	MXSD_PARTICLE_CHOICE,      /**< One alternative. */
	MXSD_PARTICLE_ALL,         /**< Unordered child particles. */
	MXSD_PARTICLE_ANY          /**< Element wildcard. */
} mxsd_particle_kind_t;

/** Simple-type facet flags used by @ref mxsd_simple_t. */
enum {
	MXSD_F_LENGTH = 1u << 0,        /**< Exact length is active. */
	MXSD_F_MIN_LENGTH = 1u << 1,    /**< Minimum length is active. */
	MXSD_F_MAX_LENGTH = 1u << 2,    /**< Maximum length is active. */
	MXSD_F_MIN_INCLUSIVE = 1u << 3, /**< Inclusive lower bound is active. */
	MXSD_F_MAX_INCLUSIVE = 1u << 4, /**< Inclusive upper bound is active. */
	MXSD_F_MIN_EXCLUSIVE = 1u << 5, /**< Exclusive lower bound is active. */
	MXSD_F_MAX_EXCLUSIVE = 1u << 6, /**< Exclusive upper bound is active. */
	MXSD_F_PATTERN = 1u << 7        /**< Pattern constraint is active. */
};

/** Type-derivation methods used by block and final. */
enum {
	MXSD_DERIVATION_EXTENSION = 1u << 0,    /**< Complex extension. */
	MXSD_DERIVATION_RESTRICTION = 1u << 1,  /**< Simple or complex restriction. */
	MXSD_DERIVATION_SUBSTITUTION = 1u << 2, /**< Element substitution group. */
	MXSD_DERIVATION_LIST = 1u << 3,         /**< Simple list derivation. */
	MXSD_DERIVATION_UNION = 1u << 4,        /**< Simple union derivation. */
	MXSD_DERIVATION_ALL = 0x1fu             /**< Every supported method. */
};

struct mxsd_simple;

/** Reference to a built-in or generated simple type. */
typedef struct {
	mxsd_type_t type;                 /**< Built-in primitive or derived kind. */
	const struct mxsd_simple *simple; /**< Generated descriptor, or @c NULL. */
} mxsd_simple_ref_t;

/** Generated descriptor for a restricted, list or union simple type. */
typedef struct mxsd_simple {
	mxsd_type_t base;                          /**< Primitive base type. */
	unsigned facets;                           /**< Bitwise combination of @c MXSD_F_*. */
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
	mxsd_whitespace_t whitespace;              /**< Normalization applied before validation. */
	mxsd_simple_variety_t variety;             /**< Atomic, list or union variety. */
	mxsd_simple_ref_t item_type;               /**< Item type for @ref MXSD_SIMPLE_LIST. */
	const mxsd_simple_ref_t *member_types;     /**< Union member types. */
	size_t member_type_count;                  /**< Number of union member types. */
	const struct mxsd_simple *base_definition; /**< Named base definition. */
	unsigned derivation_method;                /**< Method used to derive this type. */
	unsigned final_methods;                    /**< Methods forbidden to descendants. */
	const char *type_name;                     /**< Named type local name, or @c NULL. */
	const char *type_namespace;                /**< Named type namespace, or @c NULL. */
} mxsd_simple_t;

struct mxsd_complex;
struct mxsd_particle;

/** Generated descriptor for an element particle. */
typedef struct {
	const char *name;                        /**< Local element name. */
	mxsd_type_t type;                        /**< Value kind. */
	const struct mxsd_complex *complex_type; /**< Complex descriptor, if any. */
	unsigned long min_occurs;                /**< Minimum occurrences. */
	unsigned long max_occurs;                /**< Maximum, or @c ULONG_MAX. */
	const mxsd_simple_t *simple_type;        /**< Simple restriction, if any. */
	const char *namespace_uri;               /**< Required namespace, or @c NULL. */
	const char *default_value;               /**< Schema default, or @c NULL. */
	const char *fixed_value;                 /**< Required fixed value, or @c NULL. */
	int nillable;                            /**< Non-zero when xsi:nil is allowed. */
	int abstract_element;                    /**< Non-zero when substitution is required. */
	unsigned block_methods;                  /**< Prohibited runtime substitutions. */
	const char *substitution_head_name;      /**< Head local name, or @c NULL. */
	const char *substitution_head_namespace; /**< Head namespace, or @c NULL. */
} mxsd_element_t;

/** Generated descriptor for an XML attribute. */
typedef struct {
	const char *name;                 /**< Local attribute name. */
	mxsd_type_t type;                 /**< Primitive value kind. */
	int required;                     /**< Non-zero when required. */
	const mxsd_simple_t *simple_type; /**< Simple restriction, if any. */
	const char *namespace_uri;        /**< Required namespace, or @c NULL. */
	const char *default_value;        /**< Value used when absent, or @c NULL. */
	const char *fixed_value;          /**< Required value when present. */
} mxsd_attribute_t;

/** XML Schema element or attribute wildcard. */
typedef struct {
	const char *namespace_constraint; /**< Original namespace constraint. */
	const char *target_namespace;     /**< Schema target namespace, or @c NULL. */
	mxsd_process_t process_contents;  /**< strict, lax or skip behavior. */
} mxsd_wildcard_t;

/** Recursive descriptor for a compiled content-model particle. */
typedef struct mxsd_particle {
	mxsd_particle_kind_t kind;            /**< Particle kind. */
	unsigned long min_occurs;             /**< Minimum repetitions. */
	unsigned long max_occurs;             /**< Maximum, or @c ULONG_MAX. */
	const mxsd_element_t *element;        /**< Element for element particles. */
	const struct mxsd_particle *children; /**< Group child particles. */
	size_t child_count;                   /**< Number of group children. */
	const mxsd_wildcard_t *wildcard;      /**< Wildcard configuration. */
} mxsd_particle_t;

/** Generated descriptor for a complex XML Schema type. */
typedef struct mxsd_complex {
	const mxsd_element_t *elements;             /**< Child particle descriptors. */
	size_t element_count;                       /**< Number of child descriptors. */
	const mxsd_attribute_t *attributes;         /**< Attribute descriptors. */
	size_t attribute_count;                     /**< Number of attribute descriptors. */
	mxsd_model_t model;                         /**< Child content model. */
	unsigned long min_occurs;                   /**< Minimum model occurrences. */
	unsigned long max_occurs;                   /**< Maximum model occurrences. */
	const mxsd_simple_t *simple_content;        /**< Text type for simpleContent. */
	mxsd_type_t simple_content_type;            /**< Built-in simpleContent kind. */
	int has_simple_content;                     /**< Non-zero for simpleContent. */
	int abstract_type;                          /**< Non-zero if direct use is forbidden. */
	const mxsd_particle_t *particle;            /**< Recursive v0.7 content model. */
	const mxsd_wildcard_t *any_attribute;       /**< Attribute wildcard, or @c NULL. */
	const struct mxsd_complex *base_definition; /**< Named base complex type. */
	unsigned derivation_method;                 /**< Extension or restriction. */
	unsigned final_methods;                     /**< Methods forbidden to descendants. */
	const char *type_name;                      /**< Named type local name, or @c NULL. */
	const char *type_namespace;                 /**< Named type namespace, or @c NULL. */
} mxsd_complex_t;

/** One expanded-name step in the restricted identity XPath subset. */
typedef struct {
	const char *name;          /**< Local name, or @c NULL for a wildcard step. */
	const char *namespace_uri; /**< Required namespace, or @c NULL. */
} mxsd_xpath_step_t;

/** Compiled restricted XPath used by identity constraints. */
typedef struct {
	const mxsd_xpath_step_t *steps; /**< Path steps. */
	size_t step_count;              /**< Number of steps. */
	int descendant;                 /**< Non-zero for a leading @c .//. */
	int attribute;                  /**< Non-zero when the final step is an attribute. */
} mxsd_xpath_t;

/** Identity-constraint category. */
typedef enum {
	MXSD_ID_UNIQUE = 0, /**< xs:unique. */
	MXSD_ID_KEY,        /**< xs:key. */
	MXSD_ID_KEYREF      /**< xs:keyref. */
} mxsd_identity_kind_t;

/** Compiled xs:unique, xs:key or xs:keyref descriptor. */
typedef struct {
	const char *name;           /**< Constraint name. */
	mxsd_identity_kind_t kind;  /**< Constraint category. */
	mxsd_xpath_t selector;      /**< Selected element path. */
	const mxsd_xpath_t *fields; /**< Tuple fields. */
	size_t field_count;         /**< Number of tuple fields. */
	size_t refer_index;         /**< Referenced key index for keyref. */
} mxsd_identity_t;

/** Global element declaration available to strict/lax wildcards. */
typedef struct {
	const char *name;                        /**< Local element name. */
	const char *namespace_uri;               /**< Namespace URI, or @c NULL. */
	mxsd_type_t type;                        /**< Value kind. */
	const struct mxsd_complex *complex_type; /**< Complex descriptor, if any. */
	const mxsd_simple_t *simple_type;        /**< Simple descriptor, if any. */
	const char *default_value;               /**< Schema default, or @c NULL. */
	const char *fixed_value;                 /**< Required fixed value, or @c NULL. */
	int nillable;                            /**< Non-zero when xsi:nil is allowed. */
	int abstract_element;                    /**< Non-zero for an abstract declaration. */
	unsigned block_methods;                  /**< Prohibited substitutions. */
	const char *substitution_head_name;      /**< Head local name, or @c NULL. */
	const char *substitution_head_namespace; /**< Head namespace, or @c NULL. */
} mxsd_global_element_t;

/** Global attribute declaration available to strict/lax wildcards. */
typedef struct {
	const char *name;                 /**< Local attribute name. */
	const char *namespace_uri;        /**< Namespace URI, or @c NULL. */
	mxsd_type_t type;                 /**< Simple value kind. */
	const mxsd_simple_t *simple_type; /**< Restricted type, or @c NULL. */
	const char *default_value;        /**< Schema default, or @c NULL. */
	const char *fixed_value;          /**< Required fixed value, or @c NULL. */
} mxsd_global_attribute_t;

/** Named type entry used to resolve runtime xsi:type values. */
typedef struct {
	const char *name;                        /**< Type local name. */
	const char *namespace_uri;               /**< Type namespace, or @c NULL. */
	mxsd_type_t type;                        /**< Value kind. */
	const struct mxsd_complex *complex_type; /**< Complex descriptor. */
	const mxsd_simple_t *simple_type;        /**< Simple descriptor. */
} mxsd_named_type_t;

/** Root descriptor consumed by the generic validation runtime. */
typedef struct {
	const char *root_name;                            /**< Root local name. */
	mxsd_type_t root_type;                            /**< Root value kind. */
	const mxsd_complex_t *root_complex;               /**< Root complex descriptor. */
	const mxsd_simple_t *root_simple;                 /**< Root simple restriction. */
	const char *root_namespace_uri;                   /**< Root namespace, or @c NULL. */
	const mxsd_identity_t *identities;                /**< Root identity constraints. */
	size_t identity_count;                            /**< Number of identity constraints. */
	const mxsd_global_element_t *global_elements;     /**< Wildcard declaration registry. */
	size_t global_element_count;                      /**< Number of global declarations. */
	const mxsd_global_attribute_t *global_attributes; /**< Attribute registry. */
	size_t global_attribute_count;                    /**< Number of global attributes. */
	const mxsd_named_type_t *named_types;             /**< Runtime xsi:type registry. */
	size_t named_type_count;                          /**< Number of named type entries. */
	const char *default_value;                        /**< Root default, or @c NULL. */
	const char *fixed_value;                          /**< Root fixed value, or @c NULL. */
	int nillable;           /**< Non-zero when root permits xsi:nil. */
	int abstract_element;   /**< Non-zero when root is abstract. */
	unsigned block_methods; /**< Root block constraints. */
} mxsd_schema_t;

/** Internal per-element state exposed only to permit static allocation. */
typedef struct {
	const char *expected_name;          /**< Schema-owned expected local name. */
	const char *expected_namespace;     /**< Schema-owned expected namespace. */
	mxsd_type_t type;                   /**< Effective value type. */
	const mxsd_complex_t *complex_type; /**< Effective complex type. */
	const mxsd_simple_t *simple_type;   /**< Effective simple type. */
	const char *default_value;          /**< Element default. */
	const char *fixed_value;            /**< Element fixed value. */
	unsigned char seen_attributes[MXSD_MAX_ATTRIBUTES]; /**< Seen attributes. */
	unsigned long child_counts[MXSD_MAX_PARTICLES];     /**< Consumed child counts. */
	size_t sequence_index;                              /**< Current sequence position. */
	size_t text_length;                                 /**< Bytes currently in @ref text. */
	size_t child_count;                                 /**< Number of child events received. */
	int nillable;                                       /**< Whether xsi:nil is permitted. */
	int nilled;                                         /**< Whether xsi:nil was true. */
	int content_started;                                /**< Whether attributes are closed. */
	int skip_validation;                           /**< Wildcard skip/lax unknown subtree. */
	char local_name[MXSD_STREAM_NAME_CAPACITY];    /**< Actual local name. */
	char namespace_uri[MXSD_STREAM_NAME_CAPACITY]; /**< Actual namespace URI. */
	char text[MXSD_STREAM_TEXT_CAPACITY + 1u];     /**< Decoded character data. */
} mxsd_stream_frame_t;

/**
 * @brief Allocation-free state for the event-driven validator.
 *
 * Initialize this object with @ref mxsd_stream_init before sending events.
 * Its storage is bounded entirely by compile-time configuration constants.
 */
typedef struct {
	const mxsd_schema_t *schema; /**< Generated schema being validated. */
	mxsd_result_t result;        /**< Sticky result of the first failed event. */
	size_t depth;                /**< Number of currently open elements. */
	size_t event_offset;         /**< Monotonic event index used in diagnostics. */
	int finished;                /**< Non-zero after a complete root element. */
	mxsd_stream_frame_t frames[MXSD_MAX_DEPTH]; /**< Fixed element stack. */
} mxsd_stream_t;

/**
 * @brief Validate an XML document against a generated schema descriptor.
 *
 * @param schema Generated schema descriptor.
 * @param xml NUL-terminated XML document.
 *
 * @retval MXSD_OK The document is valid.
 * @retval MXSD_ERR_ARGUMENT An argument is invalid.
 * @return A structured validation result for all other outcomes.
 */
mxsd_result_t mxsd_validate(const mxsd_schema_t *schema, const char *xml);

/**
 * @brief Validate using an application-provided scratch allocator.
 *
 * Pass @c NULL as @p allocator to select zero-heap operation. Schemas with
 * identity constraints then return @ref MXSD_ERR_RESOURCE_LIMIT.
 */
mxsd_result_t mxsd_validate_with_allocator(const mxsd_schema_t *schema, const char *xml,
					   const mxsd_allocator_t *allocator);

/** Initialize an event-driven validation context. */
mxsd_result_t mxsd_stream_init(mxsd_stream_t *stream, const mxsd_schema_t *schema);

/** Begin an element identified by its expanded name. */
mxsd_result_t mxsd_start_element(mxsd_stream_t *stream, const char *namespace_uri,
				 const char *local_name);

/** Add an attribute to the current start tag using its expanded name. */
mxsd_result_t mxsd_attribute(mxsd_stream_t *stream, const char *namespace_uri,
			     const char *local_name, const char *value);

/** Apply an already namespace-resolved xsi:type to the current element. */
mxsd_result_t mxsd_xsi_type(mxsd_stream_t *stream, const char *type_namespace_uri,
			    const char *type_local_name);

/** Close the attribute phase of the current start tag. */
mxsd_result_t mxsd_start_content(mxsd_stream_t *stream);

/** Supply decoded character data for the current element. */
mxsd_result_t mxsd_text(mxsd_stream_t *stream, const char *text, size_t length);

/** End the current element, checking its expanded name and content. */
mxsd_result_t mxsd_end_element(mxsd_stream_t *stream, const char *namespace_uri,
			       const char *local_name);

/** Verify that exactly one complete root element was supplied. */
mxsd_result_t mxsd_stream_finish(mxsd_stream_t *stream);

/**
 * @brief Get a stable human-readable description of an error code.
 *
 * @param error Error code to describe.
 *
 * @return A pointer to a static NUL-terminated string.
 */
const char *mxsd_error_string(mxsd_error_t error);

/** @} */

#ifdef __cplusplus
}
#endif
#endif
