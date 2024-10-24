/** \file lvstsheet.h
    \brief style sheet

    Implements CSS compiler for CoolReader Engine.

    Supports only subset of CSS.

    Selectors supported:

    - * { } - universal selector
    - element-name { } - selector by element name
    - element1, element2 { } - several selectors delimited by comma

    Properties supported:

    - display
    - white-space
    - text-align
    - vertical-align
    - font-family
    - font-size
    - font-style
    - font-weight
    - text-indent
    - line-height
    - width
    - height
    - margin-left
    - margin-right
    - margin-top
    - margin-bottom
    - margin


    (c) Vadim Lopatin, 2000-2006
    This source code is distributed under the terms of
    GNU General Public License.

    See LICENSE file for details.

*/


#ifndef __LVSTSHEET_H_INCLUDED__
#define __LVSTSHEET_H_INCLUDED__

#include "lvtypes.h"
#include "cssdef.h"
#include "lvstyles.h"
#include "textlang.h"

class lxmlDocBase;
struct ldomNode;

/** \brief CSS property declaration

    Currently supports only subset of properties.

    Properties supported:

    - display
    - white-space
    - text-align
    - vertical-align
    - font-family
    - font-size
    - font-style
    - font-weight
    - text-indent
    - line-height
    - width
    - height
    - margin-left
    - margin-right
    - margin-top
    - margin-bottom
    - margin
*/
class LVCssDeclaration {
private:
    int * _data;
    lUInt32 _datalen;
    bool _check_if_supported;
    bool _extra_weighted;
    bool _zero_weighted;
    bool _presentational_hint;
public:
    void apply( css_style_rec_t * style, const ldomNode * node=NULL ) const;
    bool empty() const { return _data==NULL; }
    bool parse( const char * & decl, bool higher_importance=false, lxmlDocBase * doc=NULL, lString32 codeBase=lString32::empty_str );
    bool parseAndCheckIfSupported( const char * & decl, lxmlDocBase * doc=NULL ) {
        _check_if_supported = true; // will tweak parse() behaviour and meaning of return value
        return parse(decl, false, doc);
    }
    void setExtraWeighted( bool weighted ) { _extra_weighted = weighted; }
    int  isExtraWeighted() const { return _extra_weighted; }
    void setZeroWeighted( bool zero_weighted ) { _zero_weighted = zero_weighted; }
    int  isZeroWeighted() const { return _zero_weighted; }
    void setPresentationalHint( bool presentational_hint ) { _presentational_hint = presentational_hint; }
    int  isPresentationalHint() const { return _presentational_hint; }
    lUInt32 getHash() const;
    LVCssDeclaration() : _data(NULL), _datalen(0), _check_if_supported(false),
                         _extra_weighted(false), _zero_weighted(false), _presentational_hint(false)
                         { }
    ~LVCssDeclaration() { if (_data) delete[] _data; }
};

typedef LVRef<LVCssDeclaration> LVCssDeclRef;

enum LVCssSelectorRuleType
{
    cssrt_universal,         // *
    cssrt_parent,            // E > F
    cssrt_ancessor,          // E F
    cssrt_predecessor,       // E + F
    cssrt_predsibling,       // E ~ F
    cssrt_attrset,           // E[foo]
    cssrt_attreq,            // E[foo="value"]
    cssrt_attreq_i,          // E[foo="value i"] (case insensitive)
    cssrt_attrhas,           // E[foo~="value"]
    cssrt_attrhas_i,         // E[foo~="value i"]
    cssrt_attrstarts_word,   // E[foo|="value"]
    cssrt_attrstarts_word_i, // E[foo|="value i"]
    cssrt_attrstarts,        // E[foo^="value"]
    cssrt_attrstarts_i,      // E[foo^="value i"]
    cssrt_attrends,          // E[foo$="value"]
    cssrt_attrends_i,        // E[foo$="value i"]
    cssrt_attrcontains,      // E[foo*="value"]
    cssrt_attrcontains_i,    // E[foo*="value i"]
    cssrt_id,                // E#id
    cssrt_class,             // E.class
    cssrt_pseudoclass        // E:pseudo-class, E:pseudo-class(value)
};

class LVCssSelector;
typedef LVRef<LVCssSelector> LVCssSelectorRef;

class LVCssSelectorRule
{
    //
    LVCssSelectorRuleType _type;
    lUInt16 _id;
    lUInt16 _attrid;
    LVCssSelectorRule * _next;
    lString32 _value;
    lUInt32 _valueHash = 0;
    LVCssSelectorRef _subSelectors; // For pseudoclass rules :is(...), :where(...), :not(...)
    bool checkInnerText( const ldomNode * & node ) const;
public:
    explicit LVCssSelectorRule(LVCssSelectorRuleType type)
    : _type(type), _id(0), _attrid(0), _next(NULL), _subSelectors(NULL)
    { }
    LVCssSelectorRule( LVCssSelectorRule & v );
    void setId( lUInt16 id ) { _id = id; }
    void setAttr( lUInt16 id, const lString32 value ) {
        _attrid = id;
        _value = value;
        if (_type == cssrt_class)
            _valueHash = _value.getHash();
    }
    const LVCssSelectorRule * getNext() const { return _next; }
    void setNext(LVCssSelectorRule * next) { _next = next; }
    LVCssSelectorRef getSubSelectors() const { return _subSelectors; }
    void setSubSelectors(LVCssSelectorRef subSelectors) { _subSelectors = subSelectors; }
    ~LVCssSelectorRule() { if (_next) delete _next; }
    // A fail-fast check, returning false to rule out a match.
    bool quickClassCheck(const lUInt32 *classHashes, size_t size) const;
    /// check condition for node
    bool check( const ldomNode * & node, bool allow_cache=true ) const;
    /// check next rules for node
    bool checkNextRules( const ldomNode * node, bool allow_cache=true ) const;
    /// Some selector rule types do the full rules chain check themselves
    bool isFullChecking() const { return _type == cssrt_ancessor || _type == cssrt_predsibling; }
    lUInt32 getHash() const;
    lUInt32 getWeight() const;
};

/** \brief simple CSS selector

    Currently supports only element name and universal selector.

    - * { } - universal selector
    - element-name { } - selector by element name
    - element1, element2 { } - several selectors delimited by comma
*/
class LVCssSelector {
private:


    lUInt16 _id;
    bool    _is_presentational_hint;
    LVCssDeclRef _decl;
    lUInt32 _specificity;
    int _pseudo_elem; // from enum LVCssSelectorPseudoElement, or 0
    LVCssSelector * _next;
    LVRef<LVCssSelectorRule> _rules;
public:
    LVCssSelector( LVCssSelector & v );
    LVCssSelector() : _id(0), _is_presentational_hint(false), _specificity(0), _pseudo_elem(0),  _next(NULL), _rules(NULL) { }
    explicit LVCssSelector(lUInt32 specificity) : _id(0), _is_presentational_hint(false), _specificity(specificity), _pseudo_elem(0), _next(NULL), _rules(NULL) { }
    ~LVCssSelector() { if (_next) delete _next; } // NOLINT(clang-analyzer-cplusplus.NewDelete)
    bool parse( const char * &str, lxmlDocBase * doc, bool useragent_sheet=false, bool for_functional_pseudo_class=false );
    lUInt16 getElementNameId() const { return _id; }
    bool check( const ldomNode * node, bool allow_cache=true ) const;
    bool quickClassCheck(const lUInt32 *classHashes, size_t size) const;
    void applyToPseudoElement( const ldomNode * node, css_style_rec_t * style ) const;
    void apply( const ldomNode * node, css_style_rec_t * style ) const
    {
        if ( _is_presentational_hint && STYLE_HAS_CR_HINT(style, NO_PRESENTATIONAL_CSS) ) {
            return;
        }
        if (check( node )) {
            if ( _pseudo_elem > 0 ) {
                applyToPseudoElement(node, style);
            }
            else {
                _decl->apply(style, node);
            }
            // style->flags |= STYLE_REC_FLAG_MATCHED;
            // Done in applyToPseudoElement() as currently only needed there.
            // Uncomment if more generic usage needed.
        }
    }
    void setDeclaration( LVCssDeclRef decl ) { _decl = decl; }
    void setIsPresentationalHint( bool is_presentational_hint ) { _is_presentational_hint = is_presentational_hint; }
    bool isPresentationalHint() const { return _is_presentational_hint; }
    void setSpecificity(lUInt32 specificity) { _specificity = specificity; }
    void addSpecificity(lUInt32 specificity) { _specificity += specificity; }
    lUInt32 getSpecificity() const { return _specificity; }
    LVCssSelector * getNext() const { return _next; }
    void setNext(LVCssSelector * next) { _next = next; }
    lUInt32 getHash() const;
    LVCssSelector * getCopy() const {
        // Return a copy (with everything except _next) that can
        // be delete()'d without impacting this LVCssSelector
        LVCssSelector *s = new LVCssSelector();
        s->_id = _id;
        s->_is_presentational_hint = _is_presentational_hint;
        s->_decl = _decl; // (this is a LVRef)
        s->_specificity = _specificity;
        s->_pseudo_elem = _pseudo_elem;
        s->_rules = _rules;
        return s;
    }
};


/** \brief stylesheet

    Can parse stylesheet and apply compiled rules.

    Currently supports only subset of CSS features.

    \sa LVCssSelector
    \sa LVCssDeclaration
*/
class LVStyleSheet {
    lxmlDocBase * _doc;
    bool _nested;

    int _selector_count;
    LVArray <int> _selector_count_stack;

    LVPtrVector <LVCssSelector> _selectors;
    LVPtrVector <LVPtrVector <LVCssSelector> > _stack;
    LVPtrVector <LVCssSelector> * dup()
    {
        LVPtrVector <LVCssSelector> * res = new LVPtrVector <LVCssSelector>();
        res->reserve( _selectors.length() );
        for ( int i=0; i<_selectors.length(); i++ ) {
            LVCssSelector * selector = _selectors[i];
            if ( selector )
                res->add( new LVCssSelector(*selector) );
            else
                res->add(NULL);
        }
        return res;
    }

    void set(LVPtrVector<LVCssSelector> & v );

public:

    // save current state of stylesheet
    void push()
    {
        _selector_count_stack.add( _selector_count );
        _stack.add( dup() );
    }
    // restore previously saved state
    bool pop()
    {
        // Restore original counter (so we don't overflow the 19 bits
        // of _specificity reserved for storing selector order, so up
        // to 524288, when we meet a book with 600 DocFragments each
        // including a 1000 selectors stylesheet).
        if ( !_selector_count_stack.empty() )
            _selector_count = _selector_count_stack.remove( _selector_count_stack.length()-1 );
        LVPtrVector <LVCssSelector> * v = _stack.pop();
        if ( !v )
            return false;
        set( *v );
        delete v;
        return true;
    }

    /// remove all rules from stylesheet
    void clear() {
        _selector_count = 0;
        _selector_count_stack.clear();
        _selectors.clear();
        _stack.clear();
    }
    /// set document to retrieve ID values from
    void setDocument( lxmlDocBase * doc ) { _doc = doc; }
    /// constructor
    LVStyleSheet( lxmlDocBase * doc=NULL, bool nested=false ) : _doc(doc) , _nested(nested) , _selector_count(0) { }
    /// copy constructor
    LVStyleSheet( LVStyleSheet & sheet );
    /// parse stylesheet, compile and add found rules to sheet
    bool parseAndAdvance( const char * &str, bool useragent_sheet=false, lString32 codeBase=lString32::empty_str );
    bool parse( const char * str, bool useragent_sheet=false, lString32 codeBase=lString32::empty_str ) {
        // (Need this wrapper for 'const char * &str' to work with string litteral/LCSTR()/c_str())
        const char * s = str;
        return parseAndAdvance(s, useragent_sheet, codeBase);
    }
    /// apply stylesheet to node style
    void apply( const ldomNode * node, css_style_rec_t * style ) const;
    /// calculate hash
    lUInt32 getHash() const;
    void merge(const LVStyleSheet &other);
    /// gather snippets in the provided CSS that the provided node would match
    bool gatherNodeMatchingRulesets(ldomNode * node, const char * str, bool useragent_sheet, lString8Collection & matches) const;
};

/// parse number/length value like "120px" or "90%"
bool parse_number_value( const char * & str, css_length_t & value,
                                    bool accept_percent=true,
                                    bool accept_negative=false,
                                    bool accept_auto=false,
                                    bool accept_none=false,
                                    bool accept_normal=false,
                                    bool accept_unspecified=false,
                                    bool accept_contain_cover=false,
                                    bool accept_cr_special=false,
                                    bool is_font_size=false );

/// parse color value like #334455, #345 or red
bool parse_color_value( const char * & str, css_length_t & value );

/// update (if needed) a style->content (parsed from the CSS declaration) before
//  applying to a node's style
void update_style_content_property( css_style_rec_t * style, ldomNode * node );
/// get the computed final text value for a node from its style->content
lString32 get_applied_content_property( ldomNode * node );

/// extract @import filename from beginning of CSS
bool LVProcessStyleSheetImport( const char * &str, lString8 & import_file, lxmlDocBase * doc=NULL );
/// load stylesheet from file, with processing of first @import only
bool LVLoadStylesheetFile( lString32 pathName, lString8 & css );

#endif // __LVSTSHEET_H_INCLUDED__
