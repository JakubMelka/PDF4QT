//    Copyright (C) 2021 Jakub Melka
//
//    This file is part of PDF4QT.
//
//    PDF4QT is free software: you can redistribute it and/or modify
//    it under the terms of the GNU Lesser General Public License as published by
//    the Free Software Foundation, either version 3 of the License, or
//    with the written consent of the copyright owner, any later version.
//
//    PDF4QT is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU Lesser General Public License for more details.
//
//    You should have received a copy of the GNU Lesser General Public License
//    along with PDF4QT.  If not, see <https://www.gnu.org/licenses/>.

#include "pdfxfaengine.h"

namespace pdf
{

/* START GENERATED CODE */

namespace xfa
{

class XFA_appearanceFilter;
class XFA_arc;
class XFA_area;
class XFA_assist;
class XFA_barcode;
class XFA_bind;
class XFA_bindItems;
class XFA_bookend;
class XFA_boolean;
class XFA_border;
class XFA_break;
class XFA_breakAfter;
class XFA_breakBefore;
class XFA_button;
class XFA_calculate;
class XFA_caption;
class XFA_certificate;
class XFA_certificates;
class XFA_checkButton;
class XFA_choiceList;
class XFA_color;
class XFA_comb;
class XFA_connect;
class XFA_contentArea;
class XFA_corner;
class XFA_date;
class XFA_dateTime;
class XFA_dateTimeEdit;
class XFA_decimal;
class XFA_defaultUi;
class XFA_desc;
class XFA_digestMethod;
class XFA_digestMethods;
class XFA_draw;
class XFA_edge;
class XFA_encoding;
class XFA_encodings;
class XFA_encrypt;
class XFA_encryptData;
class XFA_encryption;
class XFA_encryptionMethod;
class XFA_encryptionMethods;
class XFA_event;
class XFA_exData;
class XFA_exObject;
class XFA_exclGroup;
class XFA_execute;
class XFA_extras;
class XFA_field;
class XFA_fill;
class XFA_filter;
class XFA_float;
class XFA_font;
class XFA_format;
class XFA_handler;
class XFA_hyphenation;
class XFA_image;
class XFA_imageEdit;
class XFA_integer;
class XFA_issuers;
class XFA_items;
class XFA_keep;
class XFA_keyUsage;
class XFA_line;
class XFA_linear;
class XFA_lockDocument;
class XFA_manifest;
class XFA_margin;
class XFA_mdp;
class XFA_medium;
class XFA_message;
class XFA_numericEdit;
class XFA_occur;
class XFA_oid;
class XFA_oids;
class XFA_overflow;
class XFA_pageArea;
class XFA_pageSet;
class XFA_para;
class XFA_passwordEdit;
class XFA_pattern;
class XFA_picture;
class XFA_proto;
class XFA_radial;
class XFA_reason;
class XFA_reasons;
class XFA_rectangle;
class XFA_ref;
class XFA_script;
class XFA_setProperty;
class XFA_signData;
class XFA_signature;
class XFA_signing;
class XFA_solid;
class XFA_speak;
class XFA_stipple;
class XFA_subform;
class XFA_subformSet;
class XFA_subjectDN;
class XFA_subjectDNs;
class XFA_submit;
class XFA_template;
class XFA_text;
class XFA_textEdit;
class XFA_time;
class XFA_timeStamp;
class XFA_toolTip;
class XFA_traversal;
class XFA_traverse;
class XFA_ui;
class XFA_validate;
class XFA_value;
class XFA_variables;

class XFA_BaseNode : public XFA_AbstractNode
{
public:

    enum class ACCESS
    {
        Open,
        NonInteractive,
        Protected,
        ReadOnly,
    };

    enum class ACTION
    {
        Include,
        All,
        Exclude,
    };

    enum class ACTIVITY
    {
        Click,
        Change,
        DocClose,
        DocReady,
        Enter,
        Exit,
        Full,
        IndexChange,
        Initialize,
        MouseDown,
        MouseEnter,
        MouseExit,
        MouseUp,
        PostExecute,
        PostOpen,
        PostPrint,
        PostSave,
        PostSign,
        PostSubmit,
        PreExecute,
        PreOpen,
        PrePrint,
        PreSave,
        PreSign,
        PreSubmit,
        Ready,
        ValidationState,
    };

    enum class AFTER
    {
        Auto,
        ContentArea,
        PageArea,
        PageEven,
        PageOdd,
    };

    enum class ANCHORTYPE
    {
        TopLeft,
        BottomCenter,
        BottomLeft,
        BottomRight,
        MiddleCenter,
        MiddleLeft,
        MiddleRight,
        TopCenter,
        TopRight,
    };

    enum class ASPECT
    {
        Fit,
        Actual,
        Height,
        None,
        Width,
    };

    enum class BASEPROFILE
    {
        Full,
        InteractiveForms,
    };

    enum class BEFORE
    {
        Auto,
        ContentArea,
        PageArea,
        PageEven,
        PageOdd,
    };

    enum class BLANKORNOTBLANK
    {
        Any,
        Blank,
        NotBlank,
    };

    enum class BREAK
    {
        Close,
        Open,
    };

    enum class CAP
    {
        Square,
        Butt,
        Round,
    };

    enum class CHECKSUM
    {
        None,
        _1mod10,
        _1mod10_1mod11,
        _2mod10,
        Auto,
    };

    enum class COMMITON
    {
        Select,
        Exit,
    };

    enum class CREDENTIALSERVERPOLICY
    {
        Optional,
        Required,
    };

    enum class DATAPREP
    {
        None,
        FlateCompress,
    };

    enum class DATA
    {
        Link,
        Embed,
    };

    enum class DUPLEXIMPOSITION
    {
        LongEdge,
        ShortEdge,
    };

    enum class EXECUTETYPE
    {
        Import,
        Remerge,
    };

    enum class FORMATTEST
    {
        Warning,
        Disabled,
        Error,
    };

    enum class FORMAT
    {
        Xdp,
        Formdata,
        Pdf,
        Urlencoded,
        Xfd,
        Xml,
    };

    enum class HALIGN
    {
        Left,
        Center,
        Justify,
        JustifyAll,
        Radix,
        Right,
    };

    enum class HSCROLLPOLICY
    {
        Auto,
        Off,
        On,
    };

    enum class HAND
    {
        Even,
        Left,
        Right,
    };

    enum class HIGHLIGHT
    {
        Inverted,
        None,
        Outline,
        Push,
    };

    enum class INTACT
    {
        None,
        ContentArea,
        PageArea,
    };

    enum class JOIN
    {
        Square,
        Round,
    };

    enum class KERNINGMODE
    {
        None,
        Pair,
    };

    enum class LAYOUT
    {
        Position,
        Lr_tb,
        Rl_row,
        Rl_tb,
        Row,
        Table,
        Tb,
    };

    enum class LINETHROUGHPERIOD
    {
        All,
        Word,
    };

    enum class LINETHROUGH
    {
        _0,
        _1,
        _2,
    };

    enum class LISTEN
    {
        RefOnly,
        RefAndDescendents,
    };

    enum class MARK
    {
        Default,
        Check,
        Circle,
        Cross,
        Diamond,
        Square,
        Star,
    };

    enum class MATCH
    {
        Once,
        DataRef,
        Global,
        None,
    };

    enum class MERGEMODE
    {
        ConsumeData,
        MatchTemplate,
    };

    enum class MULTILINE
    {
        _1,
        _0,
    };

    enum class NEXT
    {
        None,
        ContentArea,
        PageArea,
    };

    enum class NULLTEST
    {
        Disabled,
        Error,
        Warning,
    };

    enum class ODDOREVEN
    {
        Any,
        Even,
        Odd,
    };

    enum class OPEN
    {
        UserControl,
        Always,
        MultiSelect,
        OnEntry,
    };

    enum class OPERATION
    {
        Encrypt,
        Decrypt,
    };

    enum class OPERATION2
    {
        Next,
        Back,
        Down,
        First,
        Left,
        Right,
        Up,
    };

    enum class OPERATION1
    {
        Sign,
        Clear,
        Verify,
    };

    enum class ORIENTATION
    {
        Portrait,
        Landscape,
    };

    enum class OVERLINEPERIOD
    {
        All,
        Word,
    };

    enum class OVERLINE
    {
        _0,
        _1,
        _2,
    };

    enum class OVERRIDE
    {
        Disabled,
        Error,
        Ignore,
        Warning,
    };

    enum class PAGEPOSITION
    {
        Any,
        First,
        Last,
        Only,
        Rest,
    };

    enum class PERMISSIONS
    {
        _2,
        _1,
        _3,
    };

    enum class PICKER
    {
        Host,
        None,
    };

    enum class PLACEMENT
    {
        Left,
        Bottom,
        Inline,
        Right,
        Top,
    };

    enum class POSTURE
    {
        Normal,
        Italic,
    };

    enum class PRESENCE
    {
        Visible,
        Hidden,
        Inactive,
        Invisible,
    };

    enum class PREVIOUS
    {
        None,
        ContentArea,
        PageArea,
    };

    enum class PRIORITY
    {
        Custom,
        Caption,
        Name,
        ToolTip,
    };

    enum class RELATION1
    {
        Ordered,
        Choice,
        Unordered,
    };

    enum class RELATION
    {
        OrderedOccurrence,
        DuplexPaginated,
        SimplexPaginated,
    };

    enum class RESTORESTATE
    {
        Manual,
        Auto,
    };

    enum class RUNAT
    {
        Client,
        Both,
        Server,
    };

    enum class SCOPE
    {
        Name,
        None,
    };

    enum class SCRIPTTEST
    {
        Error,
        Disabled,
        Warning,
    };

    enum class SHAPE
    {
        Square,
        Round,
    };

    enum class SIGNATURETYPE
    {
        Filler,
        Author,
    };

    enum class SLOPE
    {
        Backslash,
        Slash,
    };

    enum class STROKE
    {
        Solid,
        DashDot,
        DashDotDot,
        Dashed,
        Dotted,
        Embossed,
        Etched,
        Lowered,
        Raised,
    };

    enum class TARGETTYPE
    {
        Auto,
        ContentArea,
        PageArea,
        PageEven,
        PageOdd,
    };

    enum class TEXTLOCATION
    {
        Below,
        Above,
        AboveEmbedded,
        BelowEmbedded,
        None,
    };

    enum class TRANSFERENCODING1
    {
        Base64,
        None,
        Package,
    };

    enum class TRANSFERENCODING
    {
        None,
        Base64,
        Package,
    };

    enum class TRAYIN
    {
        Auto,
        Delegate,
        PageFront,
    };

    enum class TRAYOUT
    {
        Auto,
        Delegate,
    };

    enum class TYPE4
    {
        PDF1_3,
        PDF1_6,
    };

    enum class TYPE2
    {
        CrossHatch,
        CrossDiagonal,
        DiagonalLeft,
        DiagonalRight,
        Horizontal,
        Vertical,
    };

    enum class TYPE
    {
        Optional,
        Required,
    };

    enum class TYPE3
    {
        ToEdge,
        ToCenter,
    };

    enum class TYPE1
    {
        ToRight,
        ToBottom,
        ToLeft,
        ToTop,
    };

    enum class UNDERLINEPERIOD
    {
        All,
        Word,
    };

    enum class UNDERLINE
    {
        _0,
        _1,
        _2,
    };

    enum class UPSMODE
    {
        UsCarrier,
        InternationalCarrier,
        SecureSymbol,
        StandardSymbol,
    };

    enum class USAGE
    {
        ExportAndImport,
        ExportOnly,
        ImportOnly,
    };

    enum class VALIGN
    {
        Top,
        Bottom,
        Middle,
    };

    enum class VSCROLLPOLICY
    {
        Auto,
        Off,
        On,
    };

    enum class WEIGHT
    {
        Normal,
        Bold,
    };

};

class XFA_appearanceFilter : public XFA_BaseNode
{
public:

    const QString* getId() const {  return m_id.getValue(); }
    const TYPE* getType() const {  return m_type.getValue(); }
    const QString* getUse() const {  return m_use.getValue(); }
    const QString* getUsehref() const {  return m_usehref.getValue(); }


    const QString* getNodeValue() const {  return m_nodeValue.getValue(); }

private:
    /* properties */
    XFA_Attribute<QString> m_id;
    XFA_Attribute<TYPE> m_type;
    XFA_Attribute<QString> m_use;
    XFA_Attribute<QString> m_usehref;

    /* subnodes */

    XFA_Value<QString> m_nodeValue;
};

class XFA_arc : public XFA_BaseNode
{
public:

    const bool* getCircular() const {  return m_circular.getValue(); }
    const HAND* getHand() const {  return m_hand.getValue(); }
    const QString* getId() const {  return m_id.getValue(); }
    const PDFReal* getStartAngle() const {  return m_startAngle.getValue(); }
    const PDFReal* getSweepAngle() const {  return m_sweepAngle.getValue(); }
    const QString* getUse() const {  return m_use.getValue(); }
    const QString* getUsehref() const {  return m_usehref.getValue(); }

    const XFA_edge* getEdge() const {  return m_edge.getValue(); }
    const XFA_fill* getFill() const {  return m_fill.getValue(); }

private:
    /* properties */
    XFA_Attribute<bool> m_circular;
    XFA_Attribute<HAND> m_hand;
    XFA_Attribute<QString> m_id;
    XFA_Attribute<PDFReal> m_startAngle;
    XFA_Attribute<PDFReal> m_sweepAngle;
    XFA_Attribute<QString> m_use;
    XFA_Attribute<QString> m_usehref;

    /* subnodes */
    XFA_Node<XFA_edge> m_edge;
    XFA_Node<XFA_fill> m_fill;
};

class XFA_area : public XFA_BaseNode
{
public:

    const PDFInteger* getColSpan() const {  return m_colSpan.getValue(); }
    const QString* getId() const {  return m_id.getValue(); }
    const QString* getName() const {  return m_name.getValue(); }
    const QString* getRelevant() const {  return m_relevant.getValue(); }
    const QString* getUse() const {  return m_use.getValue(); }
    const QString* getUsehref() const {  return m_usehref.getValue(); }
    const XFA_Measurement* getX() const {  return m_x.getValue(); }
    const XFA_Measurement* getY() const {  return m_y.getValue(); }

    const XFA_desc* getDesc() const {  return m_desc.getValue(); }
    const XFA_extras* getExtras() const {  return m_extras.getValue(); }
    const std::vector<XFA_Node<XFA_area>>& getArea() const {  return m_area; }
    const std::vector<XFA_Node<XFA_draw>>& getDraw() const {  return m_draw; }
    const std::vector<XFA_Node<XFA_exObject>>& getExObject() const {  return m_exObject; }
    const std::vector<XFA_Node<XFA_exclGroup>>& getExclGroup() const {  return m_exclGroup; }
    const std::vector<XFA_Node<XFA_field>>& getField() const {  return m_field; }
    const std::vector<XFA_Node<XFA_subform>>& getSubform() const {  return m_subform; }
    const std::vector<XFA_Node<XFA_subformSet>>& getSubformSet() const {  return m_subformSet; }

private:
    /* properties */
    XFA_Attribute<PDFInteger> m_colSpan;
    XFA_Attribute<QString> m_id;
    XFA_Attribute<QString> m_name;
    XFA_Attribute<QString> m_relevant;
    XFA_Attribute<QString> m_use;
    XFA_Attribute<QString> m_usehref;
    XFA_Attribute<XFA_Measurement> m_x;
    XFA_Attribute<XFA_Measurement> m_y;

    /* subnodes */
    XFA_Node<XFA_desc> m_desc;
    XFA_Node<XFA_extras> m_extras;
    std::vector<XFA_Node<XFA_area>> m_area;
    std::vector<XFA_Node<XFA_draw>> m_draw;
    std::vector<XFA_Node<XFA_exObject>> m_exObject;
    std::vector<XFA_Node<XFA_exclGroup>> m_exclGroup;
    std::vector<XFA_Node<XFA_field>> m_field;
    std::vector<XFA_Node<XFA_subform>> m_subform;
    std::vector<XFA_Node<XFA_subformSet>> m_subformSet;
};

class XFA_assist : public XFA_BaseNode
{
public:

    const QString* getId() const {  return m_id.getValue(); }
    const QString* getRole() const {  return m_role.getValue(); }
    const QString* getUse() const {  return m_use.getValue(); }
    const QString* getUsehref() const {  return m_usehref.getValue(); }

    const XFA_speak* getSpeak() const {  return m_speak.getValue(); }
    const XFA_toolTip* getToolTip() const {  return m_toolTip.getValue(); }

private:
    /* properties */
    XFA_Attribute<QString> m_id;
    XFA_Attribute<QString> m_role;
    XFA_Attribute<QString> m_use;
    XFA_Attribute<QString> m_usehref;

    /* subnodes */
    XFA_Node<XFA_speak> m_speak;
    XFA_Node<XFA_toolTip> m_toolTip;
};

class XFA_barcode : public XFA_BaseNode
{
public:

    const QString* getCharEncoding() const {  return m_charEncoding.getValue(); }
    const CHECKSUM* getChecksum() const {  return m_checksum.getValue(); }
    const QString* getDataColumnCount() const {  return m_dataColumnCount.getValue(); }
    const QString* getDataLength() const {  return m_dataLength.getValue(); }
    const DATAPREP* getDataPrep() const {  return m_dataPrep.getValue(); }
    const QString* getDataRowCount() const {  return m_dataRowCount.getValue(); }
    const QString* getEndChar() const {  return m_endChar.getValue(); }
    const QString* getErrorCorrectionLevel() const {  return m_errorCorrectionLevel.getValue(); }
    const QString* getId() const {  return m_id.getValue(); }
    const XFA_Measurement* getModuleHeight() const {  return m_moduleHeight.getValue(); }
    const XFA_Measurement* getModuleWidth() const {  return m_moduleWidth.getValue(); }
    const bool* getPrintCheckDigit() const {  return m_printCheckDigit.getValue(); }
    const QString* getRowColumnRatio() const {  return m_rowColumnRatio.getValue(); }
    const QString* getStartChar() const {  return m_startChar.getValue(); }
    const TEXTLOCATION* getTextLocation() const {  return m_textLocation.getValue(); }
    const bool* getTruncate() const {  return m_truncate.getValue(); }
    const QString* getType() const {  return m_type.getValue(); }
    const UPSMODE* getUpsMode() const {  return m_upsMode.getValue(); }
    const QString* getUse() const {  return m_use.getValue(); }
    const QString* getUsehref() const {  return m_usehref.getValue(); }
    const QString* getWideNarrowRatio() const {  return m_wideNarrowRatio.getValue(); }

    const XFA_encrypt* getEncrypt() const {  return m_encrypt.getValue(); }
    const XFA_extras* getExtras() const {  return m_extras.getValue(); }

private:
    /* properties */
    XFA_Attribute<QString> m_charEncoding;
    XFA_Attribute<CHECKSUM> m_checksum;
    XFA_Attribute<QString> m_dataColumnCount;
    XFA_Attribute<QString> m_dataLength;
    XFA_Attribute<DATAPREP> m_dataPrep;
    XFA_Attribute<QString> m_dataRowCount;
    XFA_Attribute<QString> m_endChar;
    XFA_Attribute<QString> m_errorCorrectionLevel;
    XFA_Attribute<QString> m_id;
    XFA_Attribute<XFA_Measurement> m_moduleHeight;
    XFA_Attribute<XFA_Measurement> m_moduleWidth;
    XFA_Attribute<bool> m_printCheckDigit;
    XFA_Attribute<QString> m_rowColumnRatio;
    XFA_Attribute<QString> m_startChar;
    XFA_Attribute<TEXTLOCATION> m_textLocation;
    XFA_Attribute<bool> m_truncate;
    XFA_Attribute<QString> m_type;
    XFA_Attribute<UPSMODE> m_upsMode;
    XFA_Attribute<QString> m_use;
    XFA_Attribute<QString> m_usehref;
    XFA_Attribute<QString> m_wideNarrowRatio;

    /* subnodes */
    XFA_Node<XFA_encrypt> m_encrypt;
    XFA_Node<XFA_extras> m_extras;
};

class XFA_bind : public XFA_BaseNode
{
public:

    const MATCH* getMatch() const {  return m_match.getValue(); }
    const QString* getRef() const {  return m_ref.getValue(); }

    const XFA_picture* getPicture() const {  return m_picture.getValue(); }

private:
    /* properties */
    XFA_Attribute<MATCH> m_match;
    XFA_Attribute<QString> m_ref;

    /* subnodes */
    XFA_Node<XFA_picture> m_picture;
};

class XFA_bindItems : public XFA_BaseNode
{
public:

    const QString* getConnection() const {  return m_connection.getValue(); }
    const QString* getLabelRef() const {  return m_labelRef.getValue(); }
    const QString* getRef() const {  return m_ref.getValue(); }
    const QString* getValueRef() const {  return m_valueRef.getValue(); }


private:
    /* properties */
    XFA_Attribute<QString> m_connection;
    XFA_Attribute<QString> m_labelRef;
    XFA_Attribute<QString> m_ref;
    XFA_Attribute<QString> m_valueRef;

    /* subnodes */
};

class XFA_bookend : public XFA_BaseNode
{
public:

    const QString* getId() const {  return m_id.getValue(); }
    const QString* getLeader() const {  return m_leader.getValue(); }
    const QString* getTrailer() const {  return m_trailer.getValue(); }
    const QString* getUse() const {  return m_use.getValue(); }
    const QString* getUsehref() const {  return m_usehref.getValue(); }


private:
    /* properties */
    XFA_Attribute<QString> m_id;
    XFA_Attribute<QString> m_leader;
    XFA_Attribute<QString> m_trailer;
    XFA_Attribute<QString> m_use;
    XFA_Attribute<QString> m_usehref;

    /* subnodes */
};

class XFA_boolean : public XFA_BaseNode
{
public:

    const QString* getId() const {  return m_id.getValue(); }
    const QString* getName() const {  return m_name.getValue(); }
    const QString* getUse() const {  return m_use.getValue(); }
    const QString* getUsehref() const {  return m_usehref.getValue(); }


    const QString* getNodeValue() const {  return m_nodeValue.getValue(); }

private:
    /* properties */
    XFA_Attribute<QString> m_id;
    XFA_Attribute<QString> m_name;
    XFA_Attribute<QString> m_use;
    XFA_Attribute<QString> m_usehref;

    /* subnodes */

    XFA_Value<QString> m_nodeValue;
};

class XFA_border : public XFA_BaseNode
{
public:

    const BREAK* getBreak() const {  return m_break.getValue(); }
    const HAND* getHand() const {  return m_hand.getValue(); }
    const QString* getId() const {  return m_id.getValue(); }
    const PRESENCE* getPresence() const {  return m_presence.getValue(); }
    const QString* getRelevant() const {  return m_relevant.getValue(); }
    const QString* getUse() const {  return m_use.getValue(); }
    const QString* getUsehref() const {  return m_usehref.getValue(); }

    const std::vector<XFA_Node<XFA_corner>>& getCorner() const {  return m_corner; }
    const std::vector<XFA_Node<XFA_edge>>& getEdge() const {  return m_edge; }
    const XFA_extras* getExtras() const {  return m_extras.getValue(); }
    const XFA_fill* getFill() const {  return m_fill.getValue(); }
    const XFA_margin* getMargin() const {  return m_margin.getValue(); }

private:
    /* properties */
    XFA_Attribute<BREAK> m_break;
    XFA_Attribute<HAND> m_hand;
    XFA_Attribute<QString> m_id;
    XFA_Attribute<PRESENCE> m_presence;
    XFA_Attribute<QString> m_relevant;
    XFA_Attribute<QString> m_use;
    XFA_Attribute<QString> m_usehref;

    /* subnodes */
    std::vector<XFA_Node<XFA_corner>> m_corner;
    std::vector<XFA_Node<XFA_edge>> m_edge;
    XFA_Node<XFA_extras> m_extras;
    XFA_Node<XFA_fill> m_fill;
    XFA_Node<XFA_margin> m_margin;
};

class XFA_break : public XFA_BaseNode
{
public:

    const AFTER* getAfter() const {  return m_after.getValue(); }
    const QString* getAfterTarget() const {  return m_afterTarget.getValue(); }
    const BEFORE* getBefore() const {  return m_before.getValue(); }
    const QString* getBeforeTarget() const {  return m_beforeTarget.getValue(); }
    const QString* getBookendLeader() const {  return m_bookendLeader.getValue(); }
    const QString* getBookendTrailer() const {  return m_bookendTrailer.getValue(); }
    const QString* getId() const {  return m_id.getValue(); }
    const QString* getOverflowLeader() const {  return m_overflowLeader.getValue(); }
    const QString* getOverflowTarget() const {  return m_overflowTarget.getValue(); }
    const QString* getOverflowTrailer() const {  return m_overflowTrailer.getValue(); }
    const bool* getStartNew() const {  return m_startNew.getValue(); }
    const QString* getUse() const {  return m_use.getValue(); }
    const QString* getUsehref() const {  return m_usehref.getValue(); }

    const XFA_extras* getExtras() const {  return m_extras.getValue(); }

private:
    /* properties */
    XFA_Attribute<AFTER> m_after;
    XFA_Attribute<QString> m_afterTarget;
    XFA_Attribute<BEFORE> m_before;
    XFA_Attribute<QString> m_beforeTarget;
    XFA_Attribute<QString> m_bookendLeader;
    XFA_Attribute<QString> m_bookendTrailer;
    XFA_Attribute<QString> m_id;
    XFA_Attribute<QString> m_overflowLeader;
    XFA_Attribute<QString> m_overflowTarget;
    XFA_Attribute<QString> m_overflowTrailer;
    XFA_Attribute<bool> m_startNew;
    XFA_Attribute<QString> m_use;
    XFA_Attribute<QString> m_usehref;

    /* subnodes */
    XFA_Node<XFA_extras> m_extras;
};

class XFA_breakAfter : public XFA_BaseNode
{
public:

    const QString* getId() const {  return m_id.getValue(); }
    const QString* getLeader() const {  return m_leader.getValue(); }
    const bool* getStartNew() const {  return m_startNew.getValue(); }
    const QString* getTarget() const {  return m_target.getValue(); }
    const TARGETTYPE* getTargetType() const {  return m_targetType.getValue(); }
    const QString* getTrailer() const {  return m_trailer.getValue(); }
    const QString* getUse() const {  return m_use.getValue(); }
    const QString* getUsehref() const {  return m_usehref.getValue(); }

    const XFA_script* getScript() const {  return m_script.getValue(); }

private:
    /* properties */
    XFA_Attribute<QString> m_id;
    XFA_Attribute<QString> m_leader;
    XFA_Attribute<bool> m_startNew;
    XFA_Attribute<QString> m_target;
    XFA_Attribute<TARGETTYPE> m_targetType;
    XFA_Attribute<QString> m_trailer;
    XFA_Attribute<QString> m_use;
    XFA_Attribute<QString> m_usehref;

    /* subnodes */
    XFA_Node<XFA_script> m_script;
};

class XFA_breakBefore : public XFA_BaseNode
{
public:

    const QString* getId() const {  return m_id.getValue(); }
    const QString* getLeader() const {  return m_leader.getValue(); }
    const bool* getStartNew() const {  return m_startNew.getValue(); }
    const QString* getTarget() const {  return m_target.getValue(); }
    const TARGETTYPE* getTargetType() const {  return m_targetType.getValue(); }
    const QString* getTrailer() const {  return m_trailer.getValue(); }
    const QString* getUse() const {  return m_use.getValue(); }
    const QString* getUsehref() const {  return m_usehref.getValue(); }

    const XFA_script* getScript() const {  return m_script.getValue(); }

private:
    /* properties */
    XFA_Attribute<QString> m_id;
    XFA_Attribute<QString> m_leader;
    XFA_Attribute<bool> m_startNew;
    XFA_Attribute<QString> m_target;
    XFA_Attribute<TARGETTYPE> m_targetType;
    XFA_Attribute<QString> m_trailer;
    XFA_Attribute<QString> m_use;
    XFA_Attribute<QString> m_usehref;

    /* subnodes */
    XFA_Node<XFA_script> m_script;
};

class XFA_button : public XFA_BaseNode
{
public:

    const HIGHLIGHT* getHighlight() const {  return m_highlight.getValue(); }
    const QString* getId() const {  return m_id.getValue(); }
    const QString* getUse() const {  return m_use.getValue(); }
    const QString* getUsehref() const {  return m_usehref.getValue(); }

    const XFA_extras* getExtras() const {  return m_extras.getValue(); }

private:
    /* properties */
    XFA_Attribute<HIGHLIGHT> m_highlight;
    XFA_Attribute<QString> m_id;
    XFA_Attribute<QString> m_use;
    XFA_Attribute<QString> m_usehref;

    /* subnodes */
    XFA_Node<XFA_extras> m_extras;
};

class XFA_calculate : public XFA_BaseNode
{
public:

    const QString* getId() const {  return m_id.getValue(); }
    const OVERRIDE* getOverride() const {  return m_override.getValue(); }
    const QString* getUse() const {  return m_use.getValue(); }
    const QString* getUsehref() const {  return m_usehref.getValue(); }

    const XFA_extras* getExtras() const {  return m_extras.getValue(); }
    const XFA_message* getMessage() const {  return m_message.getValue(); }
    const XFA_script* getScript() const {  return m_script.getValue(); }

private:
    /* properties */
    XFA_Attribute<QString> m_id;
    XFA_Attribute<OVERRIDE> m_override;
    XFA_Attribute<QString> m_use;
    XFA_Attribute<QString> m_usehref;

    /* subnodes */
    XFA_Node<XFA_extras> m_extras;
    XFA_Node<XFA_message> m_message;
    XFA_Node<XFA_script> m_script;
};

class XFA_caption : public XFA_BaseNode
{
public:

    const QString* getId() const {  return m_id.getValue(); }
    const PLACEMENT* getPlacement() const {  return m_placement.getValue(); }
    const PRESENCE* getPresence() const {  return m_presence.getValue(); }
    const XFA_Measurement* getReserve() const {  return m_reserve.getValue(); }
    const QString* getUse() const {  return m_use.getValue(); }
    const QString* getUsehref() const {  return m_usehref.getValue(); }

    const XFA_extras* getExtras() const {  return m_extras.getValue(); }
    const XFA_font* getFont() const {  return m_font.getValue(); }
    const XFA_margin* getMargin() const {  return m_margin.getValue(); }
    const XFA_para* getPara() const {  return m_para.getValue(); }
    const XFA_value* getValue() const {  return m_value.getValue(); }

private:
    /* properties */
    XFA_Attribute<QString> m_id;
    XFA_Attribute<PLACEMENT> m_placement;
    XFA_Attribute<PRESENCE> m_presence;
    XFA_Attribute<XFA_Measurement> m_reserve;
    XFA_Attribute<QString> m_use;
    XFA_Attribute<QString> m_usehref;

    /* subnodes */
    XFA_Node<XFA_extras> m_extras;
    XFA_Node<XFA_font> m_font;
    XFA_Node<XFA_margin> m_margin;
    XFA_Node<XFA_para> m_para;
    XFA_Node<XFA_value> m_value;
};

class XFA_certificate : public XFA_BaseNode
{
public:

    const QString* getId() const {  return m_id.getValue(); }
    const QString* getName() const {  return m_name.getValue(); }
    const QString* getUse() const {  return m_use.getValue(); }
    const QString* getUsehref() const {  return m_usehref.getValue(); }


    const QString* getNodeValue() const {  return m_nodeValue.getValue(); }

private:
    /* properties */
    XFA_Attribute<QString> m_id;
    XFA_Attribute<QString> m_name;
    XFA_Attribute<QString> m_use;
    XFA_Attribute<QString> m_usehref;

    /* subnodes */

    XFA_Value<QString> m_nodeValue;
};

class XFA_certificates : public XFA_BaseNode
{
public:

    const CREDENTIALSERVERPOLICY* getCredentialServerPolicy() const {  return m_credentialServerPolicy.getValue(); }
    const QString* getId() const {  return m_id.getValue(); }
    const QString* getUrl() const {  return m_url.getValue(); }
    const QString* getUrlPolicy() const {  return m_urlPolicy.getValue(); }
    const QString* getUse() const {  return m_use.getValue(); }
    const QString* getUsehref() const {  return m_usehref.getValue(); }

    const XFA_encryption* getEncryption() const {  return m_encryption.getValue(); }
    const XFA_issuers* getIssuers() const {  return m_issuers.getValue(); }
    const XFA_keyUsage* getKeyUsage() const {  return m_keyUsage.getValue(); }
    const XFA_oids* getOids() const {  return m_oids.getValue(); }
    const XFA_signing* getSigning() const {  return m_signing.getValue(); }
    const XFA_subjectDNs* getSubjectDNs() const {  return m_subjectDNs.getValue(); }

private:
    /* properties */
    XFA_Attribute<CREDENTIALSERVERPOLICY> m_credentialServerPolicy;
    XFA_Attribute<QString> m_id;
    XFA_Attribute<QString> m_url;
    XFA_Attribute<QString> m_urlPolicy;
    XFA_Attribute<QString> m_use;
    XFA_Attribute<QString> m_usehref;

    /* subnodes */
    XFA_Node<XFA_encryption> m_encryption;
    XFA_Node<XFA_issuers> m_issuers;
    XFA_Node<XFA_keyUsage> m_keyUsage;
    XFA_Node<XFA_oids> m_oids;
    XFA_Node<XFA_signing> m_signing;
    XFA_Node<XFA_subjectDNs> m_subjectDNs;
};

class XFA_checkButton : public XFA_BaseNode
{
public:

    const QString* getId() const {  return m_id.getValue(); }
    const MARK* getMark() const {  return m_mark.getValue(); }
    const SHAPE* getShape() const {  return m_shape.getValue(); }
    const XFA_Measurement* getSize() const {  return m_size.getValue(); }
    const QString* getUse() const {  return m_use.getValue(); }
    const QString* getUsehref() const {  return m_usehref.getValue(); }

    const XFA_border* getBorder() const {  return m_border.getValue(); }
    const XFA_extras* getExtras() const {  return m_extras.getValue(); }
    const XFA_margin* getMargin() const {  return m_margin.getValue(); }

private:
    /* properties */
    XFA_Attribute<QString> m_id;
    XFA_Attribute<MARK> m_mark;
    XFA_Attribute<SHAPE> m_shape;
    XFA_Attribute<XFA_Measurement> m_size;
    XFA_Attribute<QString> m_use;
    XFA_Attribute<QString> m_usehref;

    /* subnodes */
    XFA_Node<XFA_border> m_border;
    XFA_Node<XFA_extras> m_extras;
    XFA_Node<XFA_margin> m_margin;
};

class XFA_choiceList : public XFA_BaseNode
{
public:

    const COMMITON* getCommitOn() const {  return m_commitOn.getValue(); }
    const QString* getId() const {  return m_id.getValue(); }
    const OPEN* getOpen() const {  return m_open.getValue(); }
    const bool* getTextEntry() const {  return m_textEntry.getValue(); }
    const QString* getUse() const {  return m_use.getValue(); }
    const QString* getUsehref() const {  return m_usehref.getValue(); }

    const XFA_border* getBorder() const {  return m_border.getValue(); }
    const XFA_extras* getExtras() const {  return m_extras.getValue(); }
    const XFA_margin* getMargin() const {  return m_margin.getValue(); }

private:
    /* properties */
    XFA_Attribute<COMMITON> m_commitOn;
    XFA_Attribute<QString> m_id;
    XFA_Attribute<OPEN> m_open;
    XFA_Attribute<bool> m_textEntry;
    XFA_Attribute<QString> m_use;
    XFA_Attribute<QString> m_usehref;

    /* subnodes */
    XFA_Node<XFA_border> m_border;
    XFA_Node<XFA_extras> m_extras;
    XFA_Node<XFA_margin> m_margin;
};

class XFA_color : public XFA_BaseNode
{
public:

    const QString* getCSpace() const {  return m_cSpace.getValue(); }
    const QString* getId() const {  return m_id.getValue(); }
    const QString* getUse() const {  return m_use.getValue(); }
    const QString* getUsehref() const {  return m_usehref.getValue(); }
    const QString* getValue() const {  return m_value.getValue(); }

    const XFA_extras* getExtras() const {  return m_extras.getValue(); }

private:
    /* properties */
    XFA_Attribute<QString> m_cSpace;
    XFA_Attribute<QString> m_id;
    XFA_Attribute<QString> m_use;
    XFA_Attribute<QString> m_usehref;
    XFA_Attribute<QString> m_value;

    /* subnodes */
    XFA_Node<XFA_extras> m_extras;
};

class XFA_comb : public XFA_BaseNode
{
public:

    const QString* getId() const {  return m_id.getValue(); }
    const PDFInteger* getNumberOfCells() const {  return m_numberOfCells.getValue(); }
    const QString* getUse() const {  return m_use.getValue(); }
    const QString* getUsehref() const {  return m_usehref.getValue(); }


private:
    /* properties */
    XFA_Attribute<QString> m_id;
    XFA_Attribute<PDFInteger> m_numberOfCells;
    XFA_Attribute<QString> m_use;
    XFA_Attribute<QString> m_usehref;

    /* subnodes */
};

class XFA_connect : public XFA_BaseNode
{
public:

    const QString* getConnection() const {  return m_connection.getValue(); }
    const QString* getId() const {  return m_id.getValue(); }
    const QString* getRef() const {  return m_ref.getValue(); }
    const USAGE* getUsage() const {  return m_usage.getValue(); }
    const QString* getUse() const {  return m_use.getValue(); }
    const QString* getUsehref() const {  return m_usehref.getValue(); }

    const XFA_picture* getPicture() const {  return m_picture.getValue(); }

private:
    /* properties */
    XFA_Attribute<QString> m_connection;
    XFA_Attribute<QString> m_id;
    XFA_Attribute<QString> m_ref;
    XFA_Attribute<USAGE> m_usage;
    XFA_Attribute<QString> m_use;
    XFA_Attribute<QString> m_usehref;

    /* subnodes */
    XFA_Node<XFA_picture> m_picture;
};

class XFA_contentArea : public XFA_BaseNode
{
public:

    const XFA_Measurement* getH() const {  return m_h.getValue(); }
    const QString* getId() const {  return m_id.getValue(); }
    const QString* getName() const {  return m_name.getValue(); }
    const QString* getRelevant() const {  return m_relevant.getValue(); }
    const QString* getUse() const {  return m_use.getValue(); }
    const QString* getUsehref() const {  return m_usehref.getValue(); }
    const XFA_Measurement* getW() const {  return m_w.getValue(); }
    const XFA_Measurement* getX() const {  return m_x.getValue(); }
    const XFA_Measurement* getY() const {  return m_y.getValue(); }

    const XFA_desc* getDesc() const {  return m_desc.getValue(); }
    const XFA_extras* getExtras() const {  return m_extras.getValue(); }

private:
    /* properties */
    XFA_Attribute<XFA_Measurement> m_h;
    XFA_Attribute<QString> m_id;
    XFA_Attribute<QString> m_name;
    XFA_Attribute<QString> m_relevant;
    XFA_Attribute<QString> m_use;
    XFA_Attribute<QString> m_usehref;
    XFA_Attribute<XFA_Measurement> m_w;
    XFA_Attribute<XFA_Measurement> m_x;
    XFA_Attribute<XFA_Measurement> m_y;

    /* subnodes */
    XFA_Node<XFA_desc> m_desc;
    XFA_Node<XFA_extras> m_extras;
};

class XFA_corner : public XFA_BaseNode
{
public:

    const QString* getId() const {  return m_id.getValue(); }
    const bool* getInverted() const {  return m_inverted.getValue(); }
    const JOIN* getJoin() const {  return m_join.getValue(); }
    const PRESENCE* getPresence() const {  return m_presence.getValue(); }
    const XFA_Measurement* getRadius() const {  return m_radius.getValue(); }
    const STROKE* getStroke() const {  return m_stroke.getValue(); }
    const XFA_Measurement* getThickness() const {  return m_thickness.getValue(); }
    const QString* getUse() const {  return m_use.getValue(); }
    const QString* getUsehref() const {  return m_usehref.getValue(); }

    const XFA_color* getColor() const {  return m_color.getValue(); }
    const XFA_extras* getExtras() const {  return m_extras.getValue(); }

private:
    /* properties */
    XFA_Attribute<QString> m_id;
    XFA_Attribute<bool> m_inverted;
    XFA_Attribute<JOIN> m_join;
    XFA_Attribute<PRESENCE> m_presence;
    XFA_Attribute<XFA_Measurement> m_radius;
    XFA_Attribute<STROKE> m_stroke;
    XFA_Attribute<XFA_Measurement> m_thickness;
    XFA_Attribute<QString> m_use;
    XFA_Attribute<QString> m_usehref;

    /* subnodes */
    XFA_Node<XFA_color> m_color;
    XFA_Node<XFA_extras> m_extras;
};

class XFA_date : public XFA_BaseNode
{
public:

    const QString* getId() const {  return m_id.getValue(); }
    const QString* getName() const {  return m_name.getValue(); }
    const QString* getUse() const {  return m_use.getValue(); }
    const QString* getUsehref() const {  return m_usehref.getValue(); }


    const QString* getNodeValue() const {  return m_nodeValue.getValue(); }

private:
    /* properties */
    XFA_Attribute<QString> m_id;
    XFA_Attribute<QString> m_name;
    XFA_Attribute<QString> m_use;
    XFA_Attribute<QString> m_usehref;

    /* subnodes */

    XFA_Value<QString> m_nodeValue;
};

class XFA_dateTime : public XFA_BaseNode
{
public:

    const QString* getId() const {  return m_id.getValue(); }
    const QString* getName() const {  return m_name.getValue(); }
    const QString* getUse() const {  return m_use.getValue(); }
    const QString* getUsehref() const {  return m_usehref.getValue(); }


    const QString* getNodeValue() const {  return m_nodeValue.getValue(); }

private:
    /* properties */
    XFA_Attribute<QString> m_id;
    XFA_Attribute<QString> m_name;
    XFA_Attribute<QString> m_use;
    XFA_Attribute<QString> m_usehref;

    /* subnodes */

    XFA_Value<QString> m_nodeValue;
};

class XFA_dateTimeEdit : public XFA_BaseNode
{
public:

    const HSCROLLPOLICY* getHScrollPolicy() const {  return m_hScrollPolicy.getValue(); }
    const QString* getId() const {  return m_id.getValue(); }
    const PICKER* getPicker() const {  return m_picker.getValue(); }
    const QString* getUse() const {  return m_use.getValue(); }
    const QString* getUsehref() const {  return m_usehref.getValue(); }

    const XFA_border* getBorder() const {  return m_border.getValue(); }
    const XFA_comb* getComb() const {  return m_comb.getValue(); }
    const XFA_extras* getExtras() const {  return m_extras.getValue(); }
    const XFA_margin* getMargin() const {  return m_margin.getValue(); }

private:
    /* properties */
    XFA_Attribute<HSCROLLPOLICY> m_hScrollPolicy;
    XFA_Attribute<QString> m_id;
    XFA_Attribute<PICKER> m_picker;
    XFA_Attribute<QString> m_use;
    XFA_Attribute<QString> m_usehref;

    /* subnodes */
    XFA_Node<XFA_border> m_border;
    XFA_Node<XFA_comb> m_comb;
    XFA_Node<XFA_extras> m_extras;
    XFA_Node<XFA_margin> m_margin;
};

class XFA_decimal : public XFA_BaseNode
{
public:

    const PDFInteger* getFracDigits() const {  return m_fracDigits.getValue(); }
    const QString* getId() const {  return m_id.getValue(); }
    const PDFInteger* getLeadDigits() const {  return m_leadDigits.getValue(); }
    const QString* getName() const {  return m_name.getValue(); }
    const QString* getUse() const {  return m_use.getValue(); }
    const QString* getUsehref() const {  return m_usehref.getValue(); }


    const QString* getNodeValue() const {  return m_nodeValue.getValue(); }

private:
    /* properties */
    XFA_Attribute<PDFInteger> m_fracDigits;
    XFA_Attribute<QString> m_id;
    XFA_Attribute<PDFInteger> m_leadDigits;
    XFA_Attribute<QString> m_name;
    XFA_Attribute<QString> m_use;
    XFA_Attribute<QString> m_usehref;

    /* subnodes */

    XFA_Value<QString> m_nodeValue;
};

class XFA_defaultUi : public XFA_BaseNode
{
public:

    const QString* getId() const {  return m_id.getValue(); }
    const QString* getUse() const {  return m_use.getValue(); }
    const QString* getUsehref() const {  return m_usehref.getValue(); }

    const XFA_extras* getExtras() const {  return m_extras.getValue(); }

private:
    /* properties */
    XFA_Attribute<QString> m_id;
    XFA_Attribute<QString> m_use;
    XFA_Attribute<QString> m_usehref;

    /* subnodes */
    XFA_Node<XFA_extras> m_extras;
};

class XFA_desc : public XFA_BaseNode
{
public:

    const QString* getId() const {  return m_id.getValue(); }
    const QString* getUse() const {  return m_use.getValue(); }
    const QString* getUsehref() const {  return m_usehref.getValue(); }

    const std::vector<XFA_Node<XFA_boolean>>& getBoolean() const {  return m_boolean; }
    const std::vector<XFA_Node<XFA_date>>& getDate() const {  return m_date; }
    const std::vector<XFA_Node<XFA_dateTime>>& getDateTime() const {  return m_dateTime; }
    const std::vector<XFA_Node<XFA_decimal>>& getDecimal() const {  return m_decimal; }
    const std::vector<XFA_Node<XFA_exData>>& getExData() const {  return m_exData; }
    const std::vector<XFA_Node<XFA_float>>& getFloat() const {  return m_float; }
    const std::vector<XFA_Node<XFA_image>>& getImage() const {  return m_image; }
    const std::vector<XFA_Node<XFA_integer>>& getInteger() const {  return m_integer; }
    const std::vector<XFA_Node<XFA_text>>& getText() const {  return m_text; }
    const std::vector<XFA_Node<XFA_time>>& getTime() const {  return m_time; }

private:
    /* properties */
    XFA_Attribute<QString> m_id;
    XFA_Attribute<QString> m_use;
    XFA_Attribute<QString> m_usehref;

    /* subnodes */
    std::vector<XFA_Node<XFA_boolean>> m_boolean;
    std::vector<XFA_Node<XFA_date>> m_date;
    std::vector<XFA_Node<XFA_dateTime>> m_dateTime;
    std::vector<XFA_Node<XFA_decimal>> m_decimal;
    std::vector<XFA_Node<XFA_exData>> m_exData;
    std::vector<XFA_Node<XFA_float>> m_float;
    std::vector<XFA_Node<XFA_image>> m_image;
    std::vector<XFA_Node<XFA_integer>> m_integer;
    std::vector<XFA_Node<XFA_text>> m_text;
    std::vector<XFA_Node<XFA_time>> m_time;
};

class XFA_digestMethod : public XFA_BaseNode
{
public:

    const QString* getId() const {  return m_id.getValue(); }
    const QString* getUse() const {  return m_use.getValue(); }
    const QString* getUsehref() const {  return m_usehref.getValue(); }


    const QString* getNodeValue() const {  return m_nodeValue.getValue(); }

private:
    /* properties */
    XFA_Attribute<QString> m_id;
    XFA_Attribute<QString> m_use;
    XFA_Attribute<QString> m_usehref;

    /* subnodes */

    XFA_Value<QString> m_nodeValue;
};

class XFA_digestMethods : public XFA_BaseNode
{
public:

    const QString* getId() const {  return m_id.getValue(); }
    const TYPE* getType() const {  return m_type.getValue(); }
    const QString* getUse() const {  return m_use.getValue(); }
    const QString* getUsehref() const {  return m_usehref.getValue(); }

    const std::vector<XFA_Node<XFA_digestMethod>>& getDigestMethod() const {  return m_digestMethod; }

private:
    /* properties */
    XFA_Attribute<QString> m_id;
    XFA_Attribute<TYPE> m_type;
    XFA_Attribute<QString> m_use;
    XFA_Attribute<QString> m_usehref;

    /* subnodes */
    std::vector<XFA_Node<XFA_digestMethod>> m_digestMethod;
};

class XFA_draw : public XFA_BaseNode
{
public:

    const ANCHORTYPE* getAnchorType() const {  return m_anchorType.getValue(); }
    const PDFInteger* getColSpan() const {  return m_colSpan.getValue(); }
    const XFA_Measurement* getH() const {  return m_h.getValue(); }
    const HALIGN* getHAlign() const {  return m_hAlign.getValue(); }
    const QString* getId() const {  return m_id.getValue(); }
    const QString* getLocale() const {  return m_locale.getValue(); }
    const XFA_Measurement* getMaxH() const {  return m_maxH.getValue(); }
    const XFA_Measurement* getMaxW() const {  return m_maxW.getValue(); }
    const XFA_Measurement* getMinH() const {  return m_minH.getValue(); }
    const XFA_Measurement* getMinW() const {  return m_minW.getValue(); }
    const QString* getName() const {  return m_name.getValue(); }
    const PRESENCE* getPresence() const {  return m_presence.getValue(); }
    const QString* getRelevant() const {  return m_relevant.getValue(); }
    const PDFReal* getRotate() const {  return m_rotate.getValue(); }
    const QString* getUse() const {  return m_use.getValue(); }
    const QString* getUsehref() const {  return m_usehref.getValue(); }
    const XFA_Measurement* getW() const {  return m_w.getValue(); }
    const XFA_Measurement* getX() const {  return m_x.getValue(); }
    const XFA_Measurement* getY() const {  return m_y.getValue(); }

    const XFA_assist* getAssist() const {  return m_assist.getValue(); }
    const XFA_border* getBorder() const {  return m_border.getValue(); }
    const XFA_caption* getCaption() const {  return m_caption.getValue(); }
    const XFA_desc* getDesc() const {  return m_desc.getValue(); }
    const XFA_extras* getExtras() const {  return m_extras.getValue(); }
    const XFA_font* getFont() const {  return m_font.getValue(); }
    const XFA_keep* getKeep() const {  return m_keep.getValue(); }
    const XFA_margin* getMargin() const {  return m_margin.getValue(); }
    const XFA_para* getPara() const {  return m_para.getValue(); }
    const XFA_traversal* getTraversal() const {  return m_traversal.getValue(); }
    const XFA_ui* getUi() const {  return m_ui.getValue(); }
    const XFA_value* getValue() const {  return m_value.getValue(); }
    const std::vector<XFA_Node<XFA_setProperty>>& getSetProperty() const {  return m_setProperty; }

private:
    /* properties */
    XFA_Attribute<ANCHORTYPE> m_anchorType;
    XFA_Attribute<PDFInteger> m_colSpan;
    XFA_Attribute<XFA_Measurement> m_h;
    XFA_Attribute<HALIGN> m_hAlign;
    XFA_Attribute<QString> m_id;
    XFA_Attribute<QString> m_locale;
    XFA_Attribute<XFA_Measurement> m_maxH;
    XFA_Attribute<XFA_Measurement> m_maxW;
    XFA_Attribute<XFA_Measurement> m_minH;
    XFA_Attribute<XFA_Measurement> m_minW;
    XFA_Attribute<QString> m_name;
    XFA_Attribute<PRESENCE> m_presence;
    XFA_Attribute<QString> m_relevant;
    XFA_Attribute<PDFReal> m_rotate;
    XFA_Attribute<QString> m_use;
    XFA_Attribute<QString> m_usehref;
    XFA_Attribute<XFA_Measurement> m_w;
    XFA_Attribute<XFA_Measurement> m_x;
    XFA_Attribute<XFA_Measurement> m_y;

    /* subnodes */
    XFA_Node<XFA_assist> m_assist;
    XFA_Node<XFA_border> m_border;
    XFA_Node<XFA_caption> m_caption;
    XFA_Node<XFA_desc> m_desc;
    XFA_Node<XFA_extras> m_extras;
    XFA_Node<XFA_font> m_font;
    XFA_Node<XFA_keep> m_keep;
    XFA_Node<XFA_margin> m_margin;
    XFA_Node<XFA_para> m_para;
    XFA_Node<XFA_traversal> m_traversal;
    XFA_Node<XFA_ui> m_ui;
    XFA_Node<XFA_value> m_value;
    std::vector<XFA_Node<XFA_setProperty>> m_setProperty;
};

class XFA_edge : public XFA_BaseNode
{
public:

    const CAP* getCap() const {  return m_cap.getValue(); }
    const QString* getId() const {  return m_id.getValue(); }
    const PRESENCE* getPresence() const {  return m_presence.getValue(); }
    const STROKE* getStroke() const {  return m_stroke.getValue(); }
    const XFA_Measurement* getThickness() const {  return m_thickness.getValue(); }
    const QString* getUse() const {  return m_use.getValue(); }
    const QString* getUsehref() const {  return m_usehref.getValue(); }

    const XFA_color* getColor() const {  return m_color.getValue(); }
    const XFA_extras* getExtras() const {  return m_extras.getValue(); }

private:
    /* properties */
    XFA_Attribute<CAP> m_cap;
    XFA_Attribute<QString> m_id;
    XFA_Attribute<PRESENCE> m_presence;
    XFA_Attribute<STROKE> m_stroke;
    XFA_Attribute<XFA_Measurement> m_thickness;
    XFA_Attribute<QString> m_use;
    XFA_Attribute<QString> m_usehref;

    /* subnodes */
    XFA_Node<XFA_color> m_color;
    XFA_Node<XFA_extras> m_extras;
};

class XFA_encoding : public XFA_BaseNode
{
public:

    const QString* getId() const {  return m_id.getValue(); }
    const QString* getUse() const {  return m_use.getValue(); }
    const QString* getUsehref() const {  return m_usehref.getValue(); }


    const QString* getNodeValue() const {  return m_nodeValue.getValue(); }

private:
    /* properties */
    XFA_Attribute<QString> m_id;
    XFA_Attribute<QString> m_use;
    XFA_Attribute<QString> m_usehref;

    /* subnodes */

    XFA_Value<QString> m_nodeValue;
};

class XFA_encodings : public XFA_BaseNode
{
public:

    const QString* getId() const {  return m_id.getValue(); }
    const TYPE* getType() const {  return m_type.getValue(); }
    const QString* getUse() const {  return m_use.getValue(); }
    const QString* getUsehref() const {  return m_usehref.getValue(); }

    const std::vector<XFA_Node<XFA_encoding>>& getEncoding() const {  return m_encoding; }

private:
    /* properties */
    XFA_Attribute<QString> m_id;
    XFA_Attribute<TYPE> m_type;
    XFA_Attribute<QString> m_use;
    XFA_Attribute<QString> m_usehref;

    /* subnodes */
    std::vector<XFA_Node<XFA_encoding>> m_encoding;
};

class XFA_encrypt : public XFA_BaseNode
{
public:

    const QString* getId() const {  return m_id.getValue(); }
    const QString* getUse() const {  return m_use.getValue(); }
    const QString* getUsehref() const {  return m_usehref.getValue(); }

    const XFA_certificate* getCertificate() const {  return m_certificate.getValue(); }

private:
    /* properties */
    XFA_Attribute<QString> m_id;
    XFA_Attribute<QString> m_use;
    XFA_Attribute<QString> m_usehref;

    /* subnodes */
    XFA_Node<XFA_certificate> m_certificate;
};

class XFA_encryptData : public XFA_BaseNode
{
public:

    const QString* getId() const {  return m_id.getValue(); }
    const OPERATION* getOperation() const {  return m_operation.getValue(); }
    const QString* getTarget() const {  return m_target.getValue(); }
    const QString* getUse() const {  return m_use.getValue(); }
    const QString* getUsehref() const {  return m_usehref.getValue(); }

    const XFA_filter* getFilter() const {  return m_filter.getValue(); }
    const XFA_manifest* getManifest() const {  return m_manifest.getValue(); }

private:
    /* properties */
    XFA_Attribute<QString> m_id;
    XFA_Attribute<OPERATION> m_operation;
    XFA_Attribute<QString> m_target;
    XFA_Attribute<QString> m_use;
    XFA_Attribute<QString> m_usehref;

    /* subnodes */
    XFA_Node<XFA_filter> m_filter;
    XFA_Node<XFA_manifest> m_manifest;
};

class XFA_encryption : public XFA_BaseNode
{
public:

    const QString* getId() const {  return m_id.getValue(); }
    const TYPE* getType() const {  return m_type.getValue(); }
    const QString* getUse() const {  return m_use.getValue(); }
    const QString* getUsehref() const {  return m_usehref.getValue(); }

    const std::vector<XFA_Node<XFA_certificate>>& getCertificate() const {  return m_certificate; }

private:
    /* properties */
    XFA_Attribute<QString> m_id;
    XFA_Attribute<TYPE> m_type;
    XFA_Attribute<QString> m_use;
    XFA_Attribute<QString> m_usehref;

    /* subnodes */
    std::vector<XFA_Node<XFA_certificate>> m_certificate;
};

class XFA_encryptionMethod : public XFA_BaseNode
{
public:

    const QString* getId() const {  return m_id.getValue(); }
    const QString* getUse() const {  return m_use.getValue(); }
    const QString* getUsehref() const {  return m_usehref.getValue(); }


    const QString* getNodeValue() const {  return m_nodeValue.getValue(); }

private:
    /* properties */
    XFA_Attribute<QString> m_id;
    XFA_Attribute<QString> m_use;
    XFA_Attribute<QString> m_usehref;

    /* subnodes */

    XFA_Value<QString> m_nodeValue;
};

class XFA_encryptionMethods : public XFA_BaseNode
{
public:

    const QString* getId() const {  return m_id.getValue(); }
    const TYPE* getType() const {  return m_type.getValue(); }
    const QString* getUse() const {  return m_use.getValue(); }
    const QString* getUsehref() const {  return m_usehref.getValue(); }

    const std::vector<XFA_Node<XFA_encryptionMethod>>& getEncryptionMethod() const {  return m_encryptionMethod; }

private:
    /* properties */
    XFA_Attribute<QString> m_id;
    XFA_Attribute<TYPE> m_type;
    XFA_Attribute<QString> m_use;
    XFA_Attribute<QString> m_usehref;

    /* subnodes */
    std::vector<XFA_Node<XFA_encryptionMethod>> m_encryptionMethod;
};

class XFA_event : public XFA_BaseNode
{
public:

    const ACTIVITY* getActivity() const {  return m_activity.getValue(); }
    const QString* getId() const {  return m_id.getValue(); }
    const LISTEN* getListen() const {  return m_listen.getValue(); }
    const QString* getName() const {  return m_name.getValue(); }
    const QString* getRef() const {  return m_ref.getValue(); }
    const QString* getUse() const {  return m_use.getValue(); }
    const QString* getUsehref() const {  return m_usehref.getValue(); }

    const XFA_extras* getExtras() const {  return m_extras.getValue(); }
    const XFA_encryptData* getEncryptData() const {  return m_encryptData.getValue(); }
    const XFA_execute* getExecute() const {  return m_execute.getValue(); }
    const XFA_script* getScript() const {  return m_script.getValue(); }
    const XFA_signData* getSignData() const {  return m_signData.getValue(); }
    const XFA_submit* getSubmit() const {  return m_submit.getValue(); }

private:
    /* properties */
    XFA_Attribute<ACTIVITY> m_activity;
    XFA_Attribute<QString> m_id;
    XFA_Attribute<LISTEN> m_listen;
    XFA_Attribute<QString> m_name;
    XFA_Attribute<QString> m_ref;
    XFA_Attribute<QString> m_use;
    XFA_Attribute<QString> m_usehref;

    /* subnodes */
    XFA_Node<XFA_extras> m_extras;
    XFA_Node<XFA_encryptData> m_encryptData;
    XFA_Node<XFA_execute> m_execute;
    XFA_Node<XFA_script> m_script;
    XFA_Node<XFA_signData> m_signData;
    XFA_Node<XFA_submit> m_submit;
};

class XFA_exData : public XFA_BaseNode
{
public:

    const QString* getContentType() const {  return m_contentType.getValue(); }
    const QString* getHref() const {  return m_href.getValue(); }
    const QString* getId() const {  return m_id.getValue(); }
    const PDFInteger* getMaxLength() const {  return m_maxLength.getValue(); }
    const QString* getName() const {  return m_name.getValue(); }
    const QString* getRid() const {  return m_rid.getValue(); }
    const TRANSFERENCODING* getTransferEncoding() const {  return m_transferEncoding.getValue(); }
    const QString* getUse() const {  return m_use.getValue(); }
    const QString* getUsehref() const {  return m_usehref.getValue(); }


    const QString* getNodeValue() const {  return m_nodeValue.getValue(); }

private:
    /* properties */
    XFA_Attribute<QString> m_contentType;
    XFA_Attribute<QString> m_href;
    XFA_Attribute<QString> m_id;
    XFA_Attribute<PDFInteger> m_maxLength;
    XFA_Attribute<QString> m_name;
    XFA_Attribute<QString> m_rid;
    XFA_Attribute<TRANSFERENCODING> m_transferEncoding;
    XFA_Attribute<QString> m_use;
    XFA_Attribute<QString> m_usehref;

    /* subnodes */

    XFA_Value<QString> m_nodeValue;
};

class XFA_exObject : public XFA_BaseNode
{
public:

    const QString* getArchive() const {  return m_archive.getValue(); }
    const QString* getClassId() const {  return m_classId.getValue(); }
    const QString* getCodeBase() const {  return m_codeBase.getValue(); }
    const QString* getCodeType() const {  return m_codeType.getValue(); }
    const QString* getId() const {  return m_id.getValue(); }
    const QString* getName() const {  return m_name.getValue(); }
    const QString* getUse() const {  return m_use.getValue(); }
    const QString* getUsehref() const {  return m_usehref.getValue(); }

    const XFA_extras* getExtras() const {  return m_extras.getValue(); }
    const std::vector<XFA_Node<XFA_boolean>>& getBoolean() const {  return m_boolean; }
    const std::vector<XFA_Node<XFA_date>>& getDate() const {  return m_date; }
    const std::vector<XFA_Node<XFA_dateTime>>& getDateTime() const {  return m_dateTime; }
    const std::vector<XFA_Node<XFA_decimal>>& getDecimal() const {  return m_decimal; }
    const std::vector<XFA_Node<XFA_exData>>& getExData() const {  return m_exData; }
    const std::vector<XFA_Node<XFA_exObject>>& getExObject() const {  return m_exObject; }
    const std::vector<XFA_Node<XFA_float>>& getFloat() const {  return m_float; }
    const std::vector<XFA_Node<XFA_image>>& getImage() const {  return m_image; }
    const std::vector<XFA_Node<XFA_integer>>& getInteger() const {  return m_integer; }
    const std::vector<XFA_Node<XFA_text>>& getText() const {  return m_text; }
    const std::vector<XFA_Node<XFA_time>>& getTime() const {  return m_time; }

private:
    /* properties */
    XFA_Attribute<QString> m_archive;
    XFA_Attribute<QString> m_classId;
    XFA_Attribute<QString> m_codeBase;
    XFA_Attribute<QString> m_codeType;
    XFA_Attribute<QString> m_id;
    XFA_Attribute<QString> m_name;
    XFA_Attribute<QString> m_use;
    XFA_Attribute<QString> m_usehref;

    /* subnodes */
    XFA_Node<XFA_extras> m_extras;
    std::vector<XFA_Node<XFA_boolean>> m_boolean;
    std::vector<XFA_Node<XFA_date>> m_date;
    std::vector<XFA_Node<XFA_dateTime>> m_dateTime;
    std::vector<XFA_Node<XFA_decimal>> m_decimal;
    std::vector<XFA_Node<XFA_exData>> m_exData;
    std::vector<XFA_Node<XFA_exObject>> m_exObject;
    std::vector<XFA_Node<XFA_float>> m_float;
    std::vector<XFA_Node<XFA_image>> m_image;
    std::vector<XFA_Node<XFA_integer>> m_integer;
    std::vector<XFA_Node<XFA_text>> m_text;
    std::vector<XFA_Node<XFA_time>> m_time;
};

class XFA_exclGroup : public XFA_BaseNode
{
public:

    const ACCESS* getAccess() const {  return m_access.getValue(); }
    const QString* getAccessKey() const {  return m_accessKey.getValue(); }
    const ANCHORTYPE* getAnchorType() const {  return m_anchorType.getValue(); }
    const PDFInteger* getColSpan() const {  return m_colSpan.getValue(); }
    const XFA_Measurement* getH() const {  return m_h.getValue(); }
    const HALIGN* getHAlign() const {  return m_hAlign.getValue(); }
    const QString* getId() const {  return m_id.getValue(); }
    const LAYOUT* getLayout() const {  return m_layout.getValue(); }
    const XFA_Measurement* getMaxH() const {  return m_maxH.getValue(); }
    const XFA_Measurement* getMaxW() const {  return m_maxW.getValue(); }
    const XFA_Measurement* getMinH() const {  return m_minH.getValue(); }
    const XFA_Measurement* getMinW() const {  return m_minW.getValue(); }
    const QString* getName() const {  return m_name.getValue(); }
    const PRESENCE* getPresence() const {  return m_presence.getValue(); }
    const QString* getRelevant() const {  return m_relevant.getValue(); }
    const QString* getUse() const {  return m_use.getValue(); }
    const QString* getUsehref() const {  return m_usehref.getValue(); }
    const XFA_Measurement* getW() const {  return m_w.getValue(); }
    const XFA_Measurement* getX() const {  return m_x.getValue(); }
    const XFA_Measurement* getY() const {  return m_y.getValue(); }

    const XFA_assist* getAssist() const {  return m_assist.getValue(); }
    const XFA_bind* getBind() const {  return m_bind.getValue(); }
    const XFA_border* getBorder() const {  return m_border.getValue(); }
    const XFA_calculate* getCalculate() const {  return m_calculate.getValue(); }
    const XFA_caption* getCaption() const {  return m_caption.getValue(); }
    const XFA_desc* getDesc() const {  return m_desc.getValue(); }
    const XFA_extras* getExtras() const {  return m_extras.getValue(); }
    const XFA_margin* getMargin() const {  return m_margin.getValue(); }
    const XFA_para* getPara() const {  return m_para.getValue(); }
    const XFA_traversal* getTraversal() const {  return m_traversal.getValue(); }
    const XFA_validate* getValidate() const {  return m_validate.getValue(); }
    const std::vector<XFA_Node<XFA_connect>>& getConnect() const {  return m_connect; }
    const std::vector<XFA_Node<XFA_event>>& getEvent() const {  return m_event; }
    const std::vector<XFA_Node<XFA_field>>& getField() const {  return m_field; }
    const std::vector<XFA_Node<XFA_setProperty>>& getSetProperty() const {  return m_setProperty; }

private:
    /* properties */
    XFA_Attribute<ACCESS> m_access;
    XFA_Attribute<QString> m_accessKey;
    XFA_Attribute<ANCHORTYPE> m_anchorType;
    XFA_Attribute<PDFInteger> m_colSpan;
    XFA_Attribute<XFA_Measurement> m_h;
    XFA_Attribute<HALIGN> m_hAlign;
    XFA_Attribute<QString> m_id;
    XFA_Attribute<LAYOUT> m_layout;
    XFA_Attribute<XFA_Measurement> m_maxH;
    XFA_Attribute<XFA_Measurement> m_maxW;
    XFA_Attribute<XFA_Measurement> m_minH;
    XFA_Attribute<XFA_Measurement> m_minW;
    XFA_Attribute<QString> m_name;
    XFA_Attribute<PRESENCE> m_presence;
    XFA_Attribute<QString> m_relevant;
    XFA_Attribute<QString> m_use;
    XFA_Attribute<QString> m_usehref;
    XFA_Attribute<XFA_Measurement> m_w;
    XFA_Attribute<XFA_Measurement> m_x;
    XFA_Attribute<XFA_Measurement> m_y;

    /* subnodes */
    XFA_Node<XFA_assist> m_assist;
    XFA_Node<XFA_bind> m_bind;
    XFA_Node<XFA_border> m_border;
    XFA_Node<XFA_calculate> m_calculate;
    XFA_Node<XFA_caption> m_caption;
    XFA_Node<XFA_desc> m_desc;
    XFA_Node<XFA_extras> m_extras;
    XFA_Node<XFA_margin> m_margin;
    XFA_Node<XFA_para> m_para;
    XFA_Node<XFA_traversal> m_traversal;
    XFA_Node<XFA_validate> m_validate;
    std::vector<XFA_Node<XFA_connect>> m_connect;
    std::vector<XFA_Node<XFA_event>> m_event;
    std::vector<XFA_Node<XFA_field>> m_field;
    std::vector<XFA_Node<XFA_setProperty>> m_setProperty;
};

class XFA_execute : public XFA_BaseNode
{
public:

    const QString* getConnection() const {  return m_connection.getValue(); }
    const EXECUTETYPE* getExecuteType() const {  return m_executeType.getValue(); }
    const QString* getId() const {  return m_id.getValue(); }
    const RUNAT* getRunAt() const {  return m_runAt.getValue(); }
    const QString* getUse() const {  return m_use.getValue(); }
    const QString* getUsehref() const {  return m_usehref.getValue(); }


private:
    /* properties */
    XFA_Attribute<QString> m_connection;
    XFA_Attribute<EXECUTETYPE> m_executeType;
    XFA_Attribute<QString> m_id;
    XFA_Attribute<RUNAT> m_runAt;
    XFA_Attribute<QString> m_use;
    XFA_Attribute<QString> m_usehref;

    /* subnodes */
};

class XFA_extras : public XFA_BaseNode
{
public:

    const QString* getId() const {  return m_id.getValue(); }
    const QString* getName() const {  return m_name.getValue(); }
    const QString* getUse() const {  return m_use.getValue(); }
    const QString* getUsehref() const {  return m_usehref.getValue(); }

    const std::vector<XFA_Node<XFA_boolean>>& getBoolean() const {  return m_boolean; }
    const std::vector<XFA_Node<XFA_date>>& getDate() const {  return m_date; }
    const std::vector<XFA_Node<XFA_dateTime>>& getDateTime() const {  return m_dateTime; }
    const std::vector<XFA_Node<XFA_decimal>>& getDecimal() const {  return m_decimal; }
    const std::vector<XFA_Node<XFA_exData>>& getExData() const {  return m_exData; }
    const std::vector<XFA_Node<XFA_extras>>& getExtras() const {  return m_extras; }
    const std::vector<XFA_Node<XFA_float>>& getFloat() const {  return m_float; }
    const std::vector<XFA_Node<XFA_image>>& getImage() const {  return m_image; }
    const std::vector<XFA_Node<XFA_integer>>& getInteger() const {  return m_integer; }
    const std::vector<XFA_Node<XFA_text>>& getText() const {  return m_text; }
    const std::vector<XFA_Node<XFA_time>>& getTime() const {  return m_time; }

private:
    /* properties */
    XFA_Attribute<QString> m_id;
    XFA_Attribute<QString> m_name;
    XFA_Attribute<QString> m_use;
    XFA_Attribute<QString> m_usehref;

    /* subnodes */
    std::vector<XFA_Node<XFA_boolean>> m_boolean;
    std::vector<XFA_Node<XFA_date>> m_date;
    std::vector<XFA_Node<XFA_dateTime>> m_dateTime;
    std::vector<XFA_Node<XFA_decimal>> m_decimal;
    std::vector<XFA_Node<XFA_exData>> m_exData;
    std::vector<XFA_Node<XFA_extras>> m_extras;
    std::vector<XFA_Node<XFA_float>> m_float;
    std::vector<XFA_Node<XFA_image>> m_image;
    std::vector<XFA_Node<XFA_integer>> m_integer;
    std::vector<XFA_Node<XFA_text>> m_text;
    std::vector<XFA_Node<XFA_time>> m_time;
};

class XFA_field : public XFA_BaseNode
{
public:

    const ACCESS* getAccess() const {  return m_access.getValue(); }
    const QString* getAccessKey() const {  return m_accessKey.getValue(); }
    const ANCHORTYPE* getAnchorType() const {  return m_anchorType.getValue(); }
    const PDFInteger* getColSpan() const {  return m_colSpan.getValue(); }
    const XFA_Measurement* getH() const {  return m_h.getValue(); }
    const HALIGN* getHAlign() const {  return m_hAlign.getValue(); }
    const QString* getId() const {  return m_id.getValue(); }
    const QString* getLocale() const {  return m_locale.getValue(); }
    const XFA_Measurement* getMaxH() const {  return m_maxH.getValue(); }
    const XFA_Measurement* getMaxW() const {  return m_maxW.getValue(); }
    const XFA_Measurement* getMinH() const {  return m_minH.getValue(); }
    const XFA_Measurement* getMinW() const {  return m_minW.getValue(); }
    const QString* getName() const {  return m_name.getValue(); }
    const PRESENCE* getPresence() const {  return m_presence.getValue(); }
    const QString* getRelevant() const {  return m_relevant.getValue(); }
    const PDFReal* getRotate() const {  return m_rotate.getValue(); }
    const QString* getUse() const {  return m_use.getValue(); }
    const QString* getUsehref() const {  return m_usehref.getValue(); }
    const XFA_Measurement* getW() const {  return m_w.getValue(); }
    const XFA_Measurement* getX() const {  return m_x.getValue(); }
    const XFA_Measurement* getY() const {  return m_y.getValue(); }

    const XFA_assist* getAssist() const {  return m_assist.getValue(); }
    const XFA_bind* getBind() const {  return m_bind.getValue(); }
    const XFA_border* getBorder() const {  return m_border.getValue(); }
    const XFA_calculate* getCalculate() const {  return m_calculate.getValue(); }
    const XFA_caption* getCaption() const {  return m_caption.getValue(); }
    const XFA_desc* getDesc() const {  return m_desc.getValue(); }
    const XFA_extras* getExtras() const {  return m_extras.getValue(); }
    const XFA_font* getFont() const {  return m_font.getValue(); }
    const XFA_format* getFormat() const {  return m_format.getValue(); }
    const std::vector<XFA_Node<XFA_items>>& getItems() const {  return m_items; }
    const XFA_keep* getKeep() const {  return m_keep.getValue(); }
    const XFA_margin* getMargin() const {  return m_margin.getValue(); }
    const XFA_para* getPara() const {  return m_para.getValue(); }
    const XFA_traversal* getTraversal() const {  return m_traversal.getValue(); }
    const XFA_ui* getUi() const {  return m_ui.getValue(); }
    const XFA_validate* getValidate() const {  return m_validate.getValue(); }
    const XFA_value* getValue() const {  return m_value.getValue(); }
    const std::vector<XFA_Node<XFA_bindItems>>& getBindItems() const {  return m_bindItems; }
    const std::vector<XFA_Node<XFA_connect>>& getConnect() const {  return m_connect; }
    const std::vector<XFA_Node<XFA_event>>& getEvent() const {  return m_event; }
    const std::vector<XFA_Node<XFA_setProperty>>& getSetProperty() const {  return m_setProperty; }

private:
    /* properties */
    XFA_Attribute<ACCESS> m_access;
    XFA_Attribute<QString> m_accessKey;
    XFA_Attribute<ANCHORTYPE> m_anchorType;
    XFA_Attribute<PDFInteger> m_colSpan;
    XFA_Attribute<XFA_Measurement> m_h;
    XFA_Attribute<HALIGN> m_hAlign;
    XFA_Attribute<QString> m_id;
    XFA_Attribute<QString> m_locale;
    XFA_Attribute<XFA_Measurement> m_maxH;
    XFA_Attribute<XFA_Measurement> m_maxW;
    XFA_Attribute<XFA_Measurement> m_minH;
    XFA_Attribute<XFA_Measurement> m_minW;
    XFA_Attribute<QString> m_name;
    XFA_Attribute<PRESENCE> m_presence;
    XFA_Attribute<QString> m_relevant;
    XFA_Attribute<PDFReal> m_rotate;
    XFA_Attribute<QString> m_use;
    XFA_Attribute<QString> m_usehref;
    XFA_Attribute<XFA_Measurement> m_w;
    XFA_Attribute<XFA_Measurement> m_x;
    XFA_Attribute<XFA_Measurement> m_y;

    /* subnodes */
    XFA_Node<XFA_assist> m_assist;
    XFA_Node<XFA_bind> m_bind;
    XFA_Node<XFA_border> m_border;
    XFA_Node<XFA_calculate> m_calculate;
    XFA_Node<XFA_caption> m_caption;
    XFA_Node<XFA_desc> m_desc;
    XFA_Node<XFA_extras> m_extras;
    XFA_Node<XFA_font> m_font;
    XFA_Node<XFA_format> m_format;
    std::vector<XFA_Node<XFA_items>> m_items;
    XFA_Node<XFA_keep> m_keep;
    XFA_Node<XFA_margin> m_margin;
    XFA_Node<XFA_para> m_para;
    XFA_Node<XFA_traversal> m_traversal;
    XFA_Node<XFA_ui> m_ui;
    XFA_Node<XFA_validate> m_validate;
    XFA_Node<XFA_value> m_value;
    std::vector<XFA_Node<XFA_bindItems>> m_bindItems;
    std::vector<XFA_Node<XFA_connect>> m_connect;
    std::vector<XFA_Node<XFA_event>> m_event;
    std::vector<XFA_Node<XFA_setProperty>> m_setProperty;
};

class XFA_fill : public XFA_BaseNode
{
public:

    const QString* getId() const {  return m_id.getValue(); }
    const PRESENCE* getPresence() const {  return m_presence.getValue(); }
    const QString* getUse() const {  return m_use.getValue(); }
    const QString* getUsehref() const {  return m_usehref.getValue(); }

    const XFA_color* getColor() const {  return m_color.getValue(); }
    const XFA_extras* getExtras() const {  return m_extras.getValue(); }
    const XFA_linear* getLinear() const {  return m_linear.getValue(); }
    const XFA_pattern* getPattern() const {  return m_pattern.getValue(); }
    const XFA_radial* getRadial() const {  return m_radial.getValue(); }
    const XFA_solid* getSolid() const {  return m_solid.getValue(); }
    const XFA_stipple* getStipple() const {  return m_stipple.getValue(); }

private:
    /* properties */
    XFA_Attribute<QString> m_id;
    XFA_Attribute<PRESENCE> m_presence;
    XFA_Attribute<QString> m_use;
    XFA_Attribute<QString> m_usehref;

    /* subnodes */
    XFA_Node<XFA_color> m_color;
    XFA_Node<XFA_extras> m_extras;
    XFA_Node<XFA_linear> m_linear;
    XFA_Node<XFA_pattern> m_pattern;
    XFA_Node<XFA_radial> m_radial;
    XFA_Node<XFA_solid> m_solid;
    XFA_Node<XFA_stipple> m_stipple;
};

class XFA_filter : public XFA_BaseNode
{
public:

    const QString* getAddRevocationInfo() const {  return m_addRevocationInfo.getValue(); }
    const QString* getId() const {  return m_id.getValue(); }
    const QString* getName() const {  return m_name.getValue(); }
    const QString* getUse() const {  return m_use.getValue(); }
    const QString* getUsehref() const {  return m_usehref.getValue(); }
    const QString* getVersion() const {  return m_version.getValue(); }

    const XFA_appearanceFilter* getAppearanceFilter() const {  return m_appearanceFilter.getValue(); }
    const XFA_certificates* getCertificates() const {  return m_certificates.getValue(); }
    const XFA_digestMethods* getDigestMethods() const {  return m_digestMethods.getValue(); }
    const XFA_encodings* getEncodings() const {  return m_encodings.getValue(); }
    const XFA_encryptionMethods* getEncryptionMethods() const {  return m_encryptionMethods.getValue(); }
    const XFA_handler* getHandler() const {  return m_handler.getValue(); }
    const XFA_lockDocument* getLockDocument() const {  return m_lockDocument.getValue(); }
    const XFA_mdp* getMdp() const {  return m_mdp.getValue(); }
    const XFA_reasons* getReasons() const {  return m_reasons.getValue(); }
    const XFA_timeStamp* getTimeStamp() const {  return m_timeStamp.getValue(); }

private:
    /* properties */
    XFA_Attribute<QString> m_addRevocationInfo;
    XFA_Attribute<QString> m_id;
    XFA_Attribute<QString> m_name;
    XFA_Attribute<QString> m_use;
    XFA_Attribute<QString> m_usehref;
    XFA_Attribute<QString> m_version;

    /* subnodes */
    XFA_Node<XFA_appearanceFilter> m_appearanceFilter;
    XFA_Node<XFA_certificates> m_certificates;
    XFA_Node<XFA_digestMethods> m_digestMethods;
    XFA_Node<XFA_encodings> m_encodings;
    XFA_Node<XFA_encryptionMethods> m_encryptionMethods;
    XFA_Node<XFA_handler> m_handler;
    XFA_Node<XFA_lockDocument> m_lockDocument;
    XFA_Node<XFA_mdp> m_mdp;
    XFA_Node<XFA_reasons> m_reasons;
    XFA_Node<XFA_timeStamp> m_timeStamp;
};

class XFA_float : public XFA_BaseNode
{
public:

    const QString* getId() const {  return m_id.getValue(); }
    const QString* getName() const {  return m_name.getValue(); }
    const QString* getUse() const {  return m_use.getValue(); }
    const QString* getUsehref() const {  return m_usehref.getValue(); }


    const QString* getNodeValue() const {  return m_nodeValue.getValue(); }

private:
    /* properties */
    XFA_Attribute<QString> m_id;
    XFA_Attribute<QString> m_name;
    XFA_Attribute<QString> m_use;
    XFA_Attribute<QString> m_usehref;

    /* subnodes */

    XFA_Value<QString> m_nodeValue;
};

class XFA_font : public XFA_BaseNode
{
public:

    const XFA_Measurement* getBaselineShift() const {  return m_baselineShift.getValue(); }
    const QString* getFontHorizontalScale() const {  return m_fontHorizontalScale.getValue(); }
    const QString* getFontVerticalScale() const {  return m_fontVerticalScale.getValue(); }
    const QString* getId() const {  return m_id.getValue(); }
    const KERNINGMODE* getKerningMode() const {  return m_kerningMode.getValue(); }
    const QString* getLetterSpacing() const {  return m_letterSpacing.getValue(); }
    const LINETHROUGH* getLineThrough() const {  return m_lineThrough.getValue(); }
    const LINETHROUGHPERIOD* getLineThroughPeriod() const {  return m_lineThroughPeriod.getValue(); }
    const OVERLINE* getOverline() const {  return m_overline.getValue(); }
    const OVERLINEPERIOD* getOverlinePeriod() const {  return m_overlinePeriod.getValue(); }
    const POSTURE* getPosture() const {  return m_posture.getValue(); }
    const XFA_Measurement* getSize() const {  return m_size.getValue(); }
    const QString* getTypeface() const {  return m_typeface.getValue(); }
    const UNDERLINE* getUnderline() const {  return m_underline.getValue(); }
    const UNDERLINEPERIOD* getUnderlinePeriod() const {  return m_underlinePeriod.getValue(); }
    const QString* getUse() const {  return m_use.getValue(); }
    const QString* getUsehref() const {  return m_usehref.getValue(); }
    const WEIGHT* getWeight() const {  return m_weight.getValue(); }

    const XFA_extras* getExtras() const {  return m_extras.getValue(); }
    const XFA_fill* getFill() const {  return m_fill.getValue(); }

private:
    /* properties */
    XFA_Attribute<XFA_Measurement> m_baselineShift;
    XFA_Attribute<QString> m_fontHorizontalScale;
    XFA_Attribute<QString> m_fontVerticalScale;
    XFA_Attribute<QString> m_id;
    XFA_Attribute<KERNINGMODE> m_kerningMode;
    XFA_Attribute<QString> m_letterSpacing;
    XFA_Attribute<LINETHROUGH> m_lineThrough;
    XFA_Attribute<LINETHROUGHPERIOD> m_lineThroughPeriod;
    XFA_Attribute<OVERLINE> m_overline;
    XFA_Attribute<OVERLINEPERIOD> m_overlinePeriod;
    XFA_Attribute<POSTURE> m_posture;
    XFA_Attribute<XFA_Measurement> m_size;
    XFA_Attribute<QString> m_typeface;
    XFA_Attribute<UNDERLINE> m_underline;
    XFA_Attribute<UNDERLINEPERIOD> m_underlinePeriod;
    XFA_Attribute<QString> m_use;
    XFA_Attribute<QString> m_usehref;
    XFA_Attribute<WEIGHT> m_weight;

    /* subnodes */
    XFA_Node<XFA_extras> m_extras;
    XFA_Node<XFA_fill> m_fill;
};

class XFA_format : public XFA_BaseNode
{
public:

    const QString* getId() const {  return m_id.getValue(); }
    const QString* getUse() const {  return m_use.getValue(); }
    const QString* getUsehref() const {  return m_usehref.getValue(); }

    const XFA_extras* getExtras() const {  return m_extras.getValue(); }
    const XFA_picture* getPicture() const {  return m_picture.getValue(); }

private:
    /* properties */
    XFA_Attribute<QString> m_id;
    XFA_Attribute<QString> m_use;
    XFA_Attribute<QString> m_usehref;

    /* subnodes */
    XFA_Node<XFA_extras> m_extras;
    XFA_Node<XFA_picture> m_picture;
};

class XFA_handler : public XFA_BaseNode
{
public:

    const QString* getId() const {  return m_id.getValue(); }
    const TYPE* getType() const {  return m_type.getValue(); }
    const QString* getUse() const {  return m_use.getValue(); }
    const QString* getUsehref() const {  return m_usehref.getValue(); }


    const QString* getNodeValue() const {  return m_nodeValue.getValue(); }

private:
    /* properties */
    XFA_Attribute<QString> m_id;
    XFA_Attribute<TYPE> m_type;
    XFA_Attribute<QString> m_use;
    XFA_Attribute<QString> m_usehref;

    /* subnodes */

    XFA_Value<QString> m_nodeValue;
};

class XFA_hyphenation : public XFA_BaseNode
{
public:

    const bool* getExcludeAllCaps() const {  return m_excludeAllCaps.getValue(); }
    const bool* getExcludeInitialCap() const {  return m_excludeInitialCap.getValue(); }
    const bool* getHyphenate() const {  return m_hyphenate.getValue(); }
    const QString* getId() const {  return m_id.getValue(); }
    const PDFInteger* getPushCharacterCount() const {  return m_pushCharacterCount.getValue(); }
    const PDFInteger* getRemainCharacterCount() const {  return m_remainCharacterCount.getValue(); }
    const QString* getUse() const {  return m_use.getValue(); }
    const QString* getUsehref() const {  return m_usehref.getValue(); }
    const PDFInteger* getWordCharacterCount() const {  return m_wordCharacterCount.getValue(); }


private:
    /* properties */
    XFA_Attribute<bool> m_excludeAllCaps;
    XFA_Attribute<bool> m_excludeInitialCap;
    XFA_Attribute<bool> m_hyphenate;
    XFA_Attribute<QString> m_id;
    XFA_Attribute<PDFInteger> m_pushCharacterCount;
    XFA_Attribute<PDFInteger> m_remainCharacterCount;
    XFA_Attribute<QString> m_use;
    XFA_Attribute<QString> m_usehref;
    XFA_Attribute<PDFInteger> m_wordCharacterCount;

    /* subnodes */
};

class XFA_image : public XFA_BaseNode
{
public:

    const ASPECT* getAspect() const {  return m_aspect.getValue(); }
    const QString* getContentType() const {  return m_contentType.getValue(); }
    const QString* getHref() const {  return m_href.getValue(); }
    const QString* getId() const {  return m_id.getValue(); }
    const QString* getName() const {  return m_name.getValue(); }
    const TRANSFERENCODING1* getTransferEncoding() const {  return m_transferEncoding.getValue(); }
    const QString* getUse() const {  return m_use.getValue(); }
    const QString* getUsehref() const {  return m_usehref.getValue(); }


    const QString* getNodeValue() const {  return m_nodeValue.getValue(); }

private:
    /* properties */
    XFA_Attribute<ASPECT> m_aspect;
    XFA_Attribute<QString> m_contentType;
    XFA_Attribute<QString> m_href;
    XFA_Attribute<QString> m_id;
    XFA_Attribute<QString> m_name;
    XFA_Attribute<TRANSFERENCODING1> m_transferEncoding;
    XFA_Attribute<QString> m_use;
    XFA_Attribute<QString> m_usehref;

    /* subnodes */

    XFA_Value<QString> m_nodeValue;
};

class XFA_imageEdit : public XFA_BaseNode
{
public:

    const DATA* getData() const {  return m_data.getValue(); }
    const QString* getId() const {  return m_id.getValue(); }
    const QString* getUse() const {  return m_use.getValue(); }
    const QString* getUsehref() const {  return m_usehref.getValue(); }

    const XFA_border* getBorder() const {  return m_border.getValue(); }
    const XFA_extras* getExtras() const {  return m_extras.getValue(); }
    const XFA_margin* getMargin() const {  return m_margin.getValue(); }

private:
    /* properties */
    XFA_Attribute<DATA> m_data;
    XFA_Attribute<QString> m_id;
    XFA_Attribute<QString> m_use;
    XFA_Attribute<QString> m_usehref;

    /* subnodes */
    XFA_Node<XFA_border> m_border;
    XFA_Node<XFA_extras> m_extras;
    XFA_Node<XFA_margin> m_margin;
};

class XFA_integer : public XFA_BaseNode
{
public:

    const QString* getId() const {  return m_id.getValue(); }
    const QString* getName() const {  return m_name.getValue(); }
    const QString* getUse() const {  return m_use.getValue(); }
    const QString* getUsehref() const {  return m_usehref.getValue(); }


    const QString* getNodeValue() const {  return m_nodeValue.getValue(); }

private:
    /* properties */
    XFA_Attribute<QString> m_id;
    XFA_Attribute<QString> m_name;
    XFA_Attribute<QString> m_use;
    XFA_Attribute<QString> m_usehref;

    /* subnodes */

    XFA_Value<QString> m_nodeValue;
};

class XFA_issuers : public XFA_BaseNode
{
public:

    const QString* getId() const {  return m_id.getValue(); }
    const TYPE* getType() const {  return m_type.getValue(); }
    const QString* getUse() const {  return m_use.getValue(); }
    const QString* getUsehref() const {  return m_usehref.getValue(); }

    const std::vector<XFA_Node<XFA_certificate>>& getCertificate() const {  return m_certificate; }

private:
    /* properties */
    XFA_Attribute<QString> m_id;
    XFA_Attribute<TYPE> m_type;
    XFA_Attribute<QString> m_use;
    XFA_Attribute<QString> m_usehref;

    /* subnodes */
    std::vector<XFA_Node<XFA_certificate>> m_certificate;
};

class XFA_items : public XFA_BaseNode
{
public:

    const QString* getId() const {  return m_id.getValue(); }
    const QString* getName() const {  return m_name.getValue(); }
    const PRESENCE* getPresence() const {  return m_presence.getValue(); }
    const QString* getRef() const {  return m_ref.getValue(); }
    const bool* getSave() const {  return m_save.getValue(); }
    const QString* getUse() const {  return m_use.getValue(); }
    const QString* getUsehref() const {  return m_usehref.getValue(); }

    const std::vector<XFA_Node<XFA_boolean>>& getBoolean() const {  return m_boolean; }
    const std::vector<XFA_Node<XFA_date>>& getDate() const {  return m_date; }
    const std::vector<XFA_Node<XFA_dateTime>>& getDateTime() const {  return m_dateTime; }
    const std::vector<XFA_Node<XFA_decimal>>& getDecimal() const {  return m_decimal; }
    const std::vector<XFA_Node<XFA_exData>>& getExData() const {  return m_exData; }
    const std::vector<XFA_Node<XFA_float>>& getFloat() const {  return m_float; }
    const std::vector<XFA_Node<XFA_image>>& getImage() const {  return m_image; }
    const std::vector<XFA_Node<XFA_integer>>& getInteger() const {  return m_integer; }
    const std::vector<XFA_Node<XFA_text>>& getText() const {  return m_text; }
    const std::vector<XFA_Node<XFA_time>>& getTime() const {  return m_time; }

private:
    /* properties */
    XFA_Attribute<QString> m_id;
    XFA_Attribute<QString> m_name;
    XFA_Attribute<PRESENCE> m_presence;
    XFA_Attribute<QString> m_ref;
    XFA_Attribute<bool> m_save;
    XFA_Attribute<QString> m_use;
    XFA_Attribute<QString> m_usehref;

    /* subnodes */
    std::vector<XFA_Node<XFA_boolean>> m_boolean;
    std::vector<XFA_Node<XFA_date>> m_date;
    std::vector<XFA_Node<XFA_dateTime>> m_dateTime;
    std::vector<XFA_Node<XFA_decimal>> m_decimal;
    std::vector<XFA_Node<XFA_exData>> m_exData;
    std::vector<XFA_Node<XFA_float>> m_float;
    std::vector<XFA_Node<XFA_image>> m_image;
    std::vector<XFA_Node<XFA_integer>> m_integer;
    std::vector<XFA_Node<XFA_text>> m_text;
    std::vector<XFA_Node<XFA_time>> m_time;
};

class XFA_keep : public XFA_BaseNode
{
public:

    const QString* getId() const {  return m_id.getValue(); }
    const INTACT* getIntact() const {  return m_intact.getValue(); }
    const NEXT* getNext() const {  return m_next.getValue(); }
    const PREVIOUS* getPrevious() const {  return m_previous.getValue(); }
    const QString* getUse() const {  return m_use.getValue(); }
    const QString* getUsehref() const {  return m_usehref.getValue(); }

    const XFA_extras* getExtras() const {  return m_extras.getValue(); }

private:
    /* properties */
    XFA_Attribute<QString> m_id;
    XFA_Attribute<INTACT> m_intact;
    XFA_Attribute<NEXT> m_next;
    XFA_Attribute<PREVIOUS> m_previous;
    XFA_Attribute<QString> m_use;
    XFA_Attribute<QString> m_usehref;

    /* subnodes */
    XFA_Node<XFA_extras> m_extras;
};

class XFA_keyUsage : public XFA_BaseNode
{
public:

    const QString* getCrlSign() const {  return m_crlSign.getValue(); }
    const QString* getDataEncipherment() const {  return m_dataEncipherment.getValue(); }
    const QString* getDecipherOnly() const {  return m_decipherOnly.getValue(); }
    const QString* getDigitalSignature() const {  return m_digitalSignature.getValue(); }
    const QString* getEncipherOnly() const {  return m_encipherOnly.getValue(); }
    const QString* getId() const {  return m_id.getValue(); }
    const QString* getKeyAgreement() const {  return m_keyAgreement.getValue(); }
    const QString* getKeyCertSign() const {  return m_keyCertSign.getValue(); }
    const QString* getKeyEncipherment() const {  return m_keyEncipherment.getValue(); }
    const QString* getNonRepudiation() const {  return m_nonRepudiation.getValue(); }
    const TYPE* getType() const {  return m_type.getValue(); }
    const QString* getUse() const {  return m_use.getValue(); }
    const QString* getUsehref() const {  return m_usehref.getValue(); }


private:
    /* properties */
    XFA_Attribute<QString> m_crlSign;
    XFA_Attribute<QString> m_dataEncipherment;
    XFA_Attribute<QString> m_decipherOnly;
    XFA_Attribute<QString> m_digitalSignature;
    XFA_Attribute<QString> m_encipherOnly;
    XFA_Attribute<QString> m_id;
    XFA_Attribute<QString> m_keyAgreement;
    XFA_Attribute<QString> m_keyCertSign;
    XFA_Attribute<QString> m_keyEncipherment;
    XFA_Attribute<QString> m_nonRepudiation;
    XFA_Attribute<TYPE> m_type;
    XFA_Attribute<QString> m_use;
    XFA_Attribute<QString> m_usehref;

    /* subnodes */
};

class XFA_line : public XFA_BaseNode
{
public:

    const HAND* getHand() const {  return m_hand.getValue(); }
    const QString* getId() const {  return m_id.getValue(); }
    const SLOPE* getSlope() const {  return m_slope.getValue(); }
    const QString* getUse() const {  return m_use.getValue(); }
    const QString* getUsehref() const {  return m_usehref.getValue(); }

    const XFA_edge* getEdge() const {  return m_edge.getValue(); }

private:
    /* properties */
    XFA_Attribute<HAND> m_hand;
    XFA_Attribute<QString> m_id;
    XFA_Attribute<SLOPE> m_slope;
    XFA_Attribute<QString> m_use;
    XFA_Attribute<QString> m_usehref;

    /* subnodes */
    XFA_Node<XFA_edge> m_edge;
};

class XFA_linear : public XFA_BaseNode
{
public:

    const QString* getId() const {  return m_id.getValue(); }
    const TYPE1* getType() const {  return m_type.getValue(); }
    const QString* getUse() const {  return m_use.getValue(); }
    const QString* getUsehref() const {  return m_usehref.getValue(); }

    const XFA_color* getColor() const {  return m_color.getValue(); }
    const XFA_extras* getExtras() const {  return m_extras.getValue(); }

private:
    /* properties */
    XFA_Attribute<QString> m_id;
    XFA_Attribute<TYPE1> m_type;
    XFA_Attribute<QString> m_use;
    XFA_Attribute<QString> m_usehref;

    /* subnodes */
    XFA_Node<XFA_color> m_color;
    XFA_Node<XFA_extras> m_extras;
};

class XFA_lockDocument : public XFA_BaseNode
{
public:

    const QString* getId() const {  return m_id.getValue(); }
    const TYPE* getType() const {  return m_type.getValue(); }
    const QString* getUse() const {  return m_use.getValue(); }
    const QString* getUsehref() const {  return m_usehref.getValue(); }


    const QString* getNodeValue() const {  return m_nodeValue.getValue(); }

private:
    /* properties */
    XFA_Attribute<QString> m_id;
    XFA_Attribute<TYPE> m_type;
    XFA_Attribute<QString> m_use;
    XFA_Attribute<QString> m_usehref;

    /* subnodes */

    XFA_Value<QString> m_nodeValue;
};

class XFA_manifest : public XFA_BaseNode
{
public:

    const ACTION* getAction() const {  return m_action.getValue(); }
    const QString* getId() const {  return m_id.getValue(); }
    const QString* getName() const {  return m_name.getValue(); }
    const QString* getUse() const {  return m_use.getValue(); }
    const QString* getUsehref() const {  return m_usehref.getValue(); }

    const XFA_extras* getExtras() const {  return m_extras.getValue(); }
    const std::vector<XFA_Node<XFA_ref>>& getRef() const {  return m_ref; }

private:
    /* properties */
    XFA_Attribute<ACTION> m_action;
    XFA_Attribute<QString> m_id;
    XFA_Attribute<QString> m_name;
    XFA_Attribute<QString> m_use;
    XFA_Attribute<QString> m_usehref;

    /* subnodes */
    XFA_Node<XFA_extras> m_extras;
    std::vector<XFA_Node<XFA_ref>> m_ref;
};

class XFA_margin : public XFA_BaseNode
{
public:

    const XFA_Measurement* getBottomInset() const {  return m_bottomInset.getValue(); }
    const QString* getId() const {  return m_id.getValue(); }
    const XFA_Measurement* getLeftInset() const {  return m_leftInset.getValue(); }
    const XFA_Measurement* getRightInset() const {  return m_rightInset.getValue(); }
    const XFA_Measurement* getTopInset() const {  return m_topInset.getValue(); }
    const QString* getUse() const {  return m_use.getValue(); }
    const QString* getUsehref() const {  return m_usehref.getValue(); }

    const XFA_extras* getExtras() const {  return m_extras.getValue(); }

private:
    /* properties */
    XFA_Attribute<XFA_Measurement> m_bottomInset;
    XFA_Attribute<QString> m_id;
    XFA_Attribute<XFA_Measurement> m_leftInset;
    XFA_Attribute<XFA_Measurement> m_rightInset;
    XFA_Attribute<XFA_Measurement> m_topInset;
    XFA_Attribute<QString> m_use;
    XFA_Attribute<QString> m_usehref;

    /* subnodes */
    XFA_Node<XFA_extras> m_extras;
};

class XFA_mdp : public XFA_BaseNode
{
public:

    const QString* getId() const {  return m_id.getValue(); }
    const PERMISSIONS* getPermissions() const {  return m_permissions.getValue(); }
    const SIGNATURETYPE* getSignatureType() const {  return m_signatureType.getValue(); }
    const QString* getUse() const {  return m_use.getValue(); }
    const QString* getUsehref() const {  return m_usehref.getValue(); }


private:
    /* properties */
    XFA_Attribute<QString> m_id;
    XFA_Attribute<PERMISSIONS> m_permissions;
    XFA_Attribute<SIGNATURETYPE> m_signatureType;
    XFA_Attribute<QString> m_use;
    XFA_Attribute<QString> m_usehref;

    /* subnodes */
};

class XFA_medium : public XFA_BaseNode
{
public:

    const QString* getId() const {  return m_id.getValue(); }
    const QString* getImagingBBox() const {  return m_imagingBBox.getValue(); }
    const XFA_Measurement* getLong() const {  return m_long.getValue(); }
    const ORIENTATION* getOrientation() const {  return m_orientation.getValue(); }
    const XFA_Measurement* getShort() const {  return m_short.getValue(); }
    const QString* getStock() const {  return m_stock.getValue(); }
    const TRAYIN* getTrayIn() const {  return m_trayIn.getValue(); }
    const TRAYOUT* getTrayOut() const {  return m_trayOut.getValue(); }
    const QString* getUse() const {  return m_use.getValue(); }
    const QString* getUsehref() const {  return m_usehref.getValue(); }


private:
    /* properties */
    XFA_Attribute<QString> m_id;
    XFA_Attribute<QString> m_imagingBBox;
    XFA_Attribute<XFA_Measurement> m_long;
    XFA_Attribute<ORIENTATION> m_orientation;
    XFA_Attribute<XFA_Measurement> m_short;
    XFA_Attribute<QString> m_stock;
    XFA_Attribute<TRAYIN> m_trayIn;
    XFA_Attribute<TRAYOUT> m_trayOut;
    XFA_Attribute<QString> m_use;
    XFA_Attribute<QString> m_usehref;

    /* subnodes */
};

class XFA_message : public XFA_BaseNode
{
public:

    const QString* getId() const {  return m_id.getValue(); }
    const QString* getUse() const {  return m_use.getValue(); }
    const QString* getUsehref() const {  return m_usehref.getValue(); }

    const std::vector<XFA_Node<XFA_text>>& getText() const {  return m_text; }

private:
    /* properties */
    XFA_Attribute<QString> m_id;
    XFA_Attribute<QString> m_use;
    XFA_Attribute<QString> m_usehref;

    /* subnodes */
    std::vector<XFA_Node<XFA_text>> m_text;
};

class XFA_numericEdit : public XFA_BaseNode
{
public:

    const HSCROLLPOLICY* getHScrollPolicy() const {  return m_hScrollPolicy.getValue(); }
    const QString* getId() const {  return m_id.getValue(); }
    const QString* getUse() const {  return m_use.getValue(); }
    const QString* getUsehref() const {  return m_usehref.getValue(); }

    const XFA_border* getBorder() const {  return m_border.getValue(); }
    const XFA_comb* getComb() const {  return m_comb.getValue(); }
    const XFA_extras* getExtras() const {  return m_extras.getValue(); }
    const XFA_margin* getMargin() const {  return m_margin.getValue(); }

private:
    /* properties */
    XFA_Attribute<HSCROLLPOLICY> m_hScrollPolicy;
    XFA_Attribute<QString> m_id;
    XFA_Attribute<QString> m_use;
    XFA_Attribute<QString> m_usehref;

    /* subnodes */
    XFA_Node<XFA_border> m_border;
    XFA_Node<XFA_comb> m_comb;
    XFA_Node<XFA_extras> m_extras;
    XFA_Node<XFA_margin> m_margin;
};

class XFA_occur : public XFA_BaseNode
{
public:

    const QString* getId() const {  return m_id.getValue(); }
    const PDFInteger* getInitial() const {  return m_initial.getValue(); }
    const PDFInteger* getMax() const {  return m_max.getValue(); }
    const PDFInteger* getMin() const {  return m_min.getValue(); }
    const QString* getUse() const {  return m_use.getValue(); }
    const QString* getUsehref() const {  return m_usehref.getValue(); }

    const XFA_extras* getExtras() const {  return m_extras.getValue(); }

private:
    /* properties */
    XFA_Attribute<QString> m_id;
    XFA_Attribute<PDFInteger> m_initial;
    XFA_Attribute<PDFInteger> m_max;
    XFA_Attribute<PDFInteger> m_min;
    XFA_Attribute<QString> m_use;
    XFA_Attribute<QString> m_usehref;

    /* subnodes */
    XFA_Node<XFA_extras> m_extras;
};

class XFA_oid : public XFA_BaseNode
{
public:

    const QString* getId() const {  return m_id.getValue(); }
    const QString* getName() const {  return m_name.getValue(); }
    const QString* getUse() const {  return m_use.getValue(); }
    const QString* getUsehref() const {  return m_usehref.getValue(); }


    const QString* getNodeValue() const {  return m_nodeValue.getValue(); }

private:
    /* properties */
    XFA_Attribute<QString> m_id;
    XFA_Attribute<QString> m_name;
    XFA_Attribute<QString> m_use;
    XFA_Attribute<QString> m_usehref;

    /* subnodes */

    XFA_Value<QString> m_nodeValue;
};

class XFA_oids : public XFA_BaseNode
{
public:

    const QString* getId() const {  return m_id.getValue(); }
    const TYPE* getType() const {  return m_type.getValue(); }
    const QString* getUse() const {  return m_use.getValue(); }
    const QString* getUsehref() const {  return m_usehref.getValue(); }

    const std::vector<XFA_Node<XFA_oid>>& getOid() const {  return m_oid; }

private:
    /* properties */
    XFA_Attribute<QString> m_id;
    XFA_Attribute<TYPE> m_type;
    XFA_Attribute<QString> m_use;
    XFA_Attribute<QString> m_usehref;

    /* subnodes */
    std::vector<XFA_Node<XFA_oid>> m_oid;
};

class XFA_overflow : public XFA_BaseNode
{
public:

    const QString* getId() const {  return m_id.getValue(); }
    const QString* getLeader() const {  return m_leader.getValue(); }
    const QString* getTarget() const {  return m_target.getValue(); }
    const QString* getTrailer() const {  return m_trailer.getValue(); }
    const QString* getUse() const {  return m_use.getValue(); }
    const QString* getUsehref() const {  return m_usehref.getValue(); }


private:
    /* properties */
    XFA_Attribute<QString> m_id;
    XFA_Attribute<QString> m_leader;
    XFA_Attribute<QString> m_target;
    XFA_Attribute<QString> m_trailer;
    XFA_Attribute<QString> m_use;
    XFA_Attribute<QString> m_usehref;

    /* subnodes */
};

class XFA_pageArea : public XFA_BaseNode
{
public:

    const BLANKORNOTBLANK* getBlankOrNotBlank() const {  return m_blankOrNotBlank.getValue(); }
    const QString* getId() const {  return m_id.getValue(); }
    const PDFInteger* getInitialNumber() const {  return m_initialNumber.getValue(); }
    const QString* getName() const {  return m_name.getValue(); }
    const PDFInteger* getNumbered() const {  return m_numbered.getValue(); }
    const ODDOREVEN* getOddOrEven() const {  return m_oddOrEven.getValue(); }
    const PAGEPOSITION* getPagePosition() const {  return m_pagePosition.getValue(); }
    const QString* getRelevant() const {  return m_relevant.getValue(); }
    const QString* getUse() const {  return m_use.getValue(); }
    const QString* getUsehref() const {  return m_usehref.getValue(); }

    const XFA_desc* getDesc() const {  return m_desc.getValue(); }
    const XFA_extras* getExtras() const {  return m_extras.getValue(); }
    const XFA_medium* getMedium() const {  return m_medium.getValue(); }
    const XFA_occur* getOccur() const {  return m_occur.getValue(); }
    const std::vector<XFA_Node<XFA_area>>& getArea() const {  return m_area; }
    const std::vector<XFA_Node<XFA_contentArea>>& getContentArea() const {  return m_contentArea; }
    const std::vector<XFA_Node<XFA_draw>>& getDraw() const {  return m_draw; }
    const std::vector<XFA_Node<XFA_exclGroup>>& getExclGroup() const {  return m_exclGroup; }
    const std::vector<XFA_Node<XFA_field>>& getField() const {  return m_field; }
    const std::vector<XFA_Node<XFA_subform>>& getSubform() const {  return m_subform; }

private:
    /* properties */
    XFA_Attribute<BLANKORNOTBLANK> m_blankOrNotBlank;
    XFA_Attribute<QString> m_id;
    XFA_Attribute<PDFInteger> m_initialNumber;
    XFA_Attribute<QString> m_name;
    XFA_Attribute<PDFInteger> m_numbered;
    XFA_Attribute<ODDOREVEN> m_oddOrEven;
    XFA_Attribute<PAGEPOSITION> m_pagePosition;
    XFA_Attribute<QString> m_relevant;
    XFA_Attribute<QString> m_use;
    XFA_Attribute<QString> m_usehref;

    /* subnodes */
    XFA_Node<XFA_desc> m_desc;
    XFA_Node<XFA_extras> m_extras;
    XFA_Node<XFA_medium> m_medium;
    XFA_Node<XFA_occur> m_occur;
    std::vector<XFA_Node<XFA_area>> m_area;
    std::vector<XFA_Node<XFA_contentArea>> m_contentArea;
    std::vector<XFA_Node<XFA_draw>> m_draw;
    std::vector<XFA_Node<XFA_exclGroup>> m_exclGroup;
    std::vector<XFA_Node<XFA_field>> m_field;
    std::vector<XFA_Node<XFA_subform>> m_subform;
};

class XFA_pageSet : public XFA_BaseNode
{
public:

    const DUPLEXIMPOSITION* getDuplexImposition() const {  return m_duplexImposition.getValue(); }
    const QString* getId() const {  return m_id.getValue(); }
    const QString* getName() const {  return m_name.getValue(); }
    const RELATION* getRelation() const {  return m_relation.getValue(); }
    const QString* getRelevant() const {  return m_relevant.getValue(); }
    const QString* getUse() const {  return m_use.getValue(); }
    const QString* getUsehref() const {  return m_usehref.getValue(); }

    const XFA_extras* getExtras() const {  return m_extras.getValue(); }
    const XFA_occur* getOccur() const {  return m_occur.getValue(); }
    const std::vector<XFA_Node<XFA_pageArea>>& getPageArea() const {  return m_pageArea; }
    const std::vector<XFA_Node<XFA_pageSet>>& getPageSet() const {  return m_pageSet; }

private:
    /* properties */
    XFA_Attribute<DUPLEXIMPOSITION> m_duplexImposition;
    XFA_Attribute<QString> m_id;
    XFA_Attribute<QString> m_name;
    XFA_Attribute<RELATION> m_relation;
    XFA_Attribute<QString> m_relevant;
    XFA_Attribute<QString> m_use;
    XFA_Attribute<QString> m_usehref;

    /* subnodes */
    XFA_Node<XFA_extras> m_extras;
    XFA_Node<XFA_occur> m_occur;
    std::vector<XFA_Node<XFA_pageArea>> m_pageArea;
    std::vector<XFA_Node<XFA_pageSet>> m_pageSet;
};

class XFA_para : public XFA_BaseNode
{
public:

    const HALIGN* getHAlign() const {  return m_hAlign.getValue(); }
    const QString* getId() const {  return m_id.getValue(); }
    const XFA_Measurement* getLineHeight() const {  return m_lineHeight.getValue(); }
    const XFA_Measurement* getMarginLeft() const {  return m_marginLeft.getValue(); }
    const XFA_Measurement* getMarginRight() const {  return m_marginRight.getValue(); }
    const PDFInteger* getOrphans() const {  return m_orphans.getValue(); }
    const QString* getPreserve() const {  return m_preserve.getValue(); }
    const XFA_Measurement* getRadixOffset() const {  return m_radixOffset.getValue(); }
    const XFA_Measurement* getSpaceAbove() const {  return m_spaceAbove.getValue(); }
    const XFA_Measurement* getSpaceBelow() const {  return m_spaceBelow.getValue(); }
    const QString* getTabDefault() const {  return m_tabDefault.getValue(); }
    const QString* getTabStops() const {  return m_tabStops.getValue(); }
    const XFA_Measurement* getTextIndent() const {  return m_textIndent.getValue(); }
    const QString* getUse() const {  return m_use.getValue(); }
    const QString* getUsehref() const {  return m_usehref.getValue(); }
    const VALIGN* getVAlign() const {  return m_vAlign.getValue(); }
    const PDFInteger* getWidows() const {  return m_widows.getValue(); }

    const XFA_hyphenation* getHyphenation() const {  return m_hyphenation.getValue(); }

private:
    /* properties */
    XFA_Attribute<HALIGN> m_hAlign;
    XFA_Attribute<QString> m_id;
    XFA_Attribute<XFA_Measurement> m_lineHeight;
    XFA_Attribute<XFA_Measurement> m_marginLeft;
    XFA_Attribute<XFA_Measurement> m_marginRight;
    XFA_Attribute<PDFInteger> m_orphans;
    XFA_Attribute<QString> m_preserve;
    XFA_Attribute<XFA_Measurement> m_radixOffset;
    XFA_Attribute<XFA_Measurement> m_spaceAbove;
    XFA_Attribute<XFA_Measurement> m_spaceBelow;
    XFA_Attribute<QString> m_tabDefault;
    XFA_Attribute<QString> m_tabStops;
    XFA_Attribute<XFA_Measurement> m_textIndent;
    XFA_Attribute<QString> m_use;
    XFA_Attribute<QString> m_usehref;
    XFA_Attribute<VALIGN> m_vAlign;
    XFA_Attribute<PDFInteger> m_widows;

    /* subnodes */
    XFA_Node<XFA_hyphenation> m_hyphenation;
};

class XFA_passwordEdit : public XFA_BaseNode
{
public:

    const HSCROLLPOLICY* getHScrollPolicy() const {  return m_hScrollPolicy.getValue(); }
    const QString* getId() const {  return m_id.getValue(); }
    const QString* getPasswordChar() const {  return m_passwordChar.getValue(); }
    const QString* getUse() const {  return m_use.getValue(); }
    const QString* getUsehref() const {  return m_usehref.getValue(); }

    const XFA_border* getBorder() const {  return m_border.getValue(); }
    const XFA_extras* getExtras() const {  return m_extras.getValue(); }
    const XFA_margin* getMargin() const {  return m_margin.getValue(); }

private:
    /* properties */
    XFA_Attribute<HSCROLLPOLICY> m_hScrollPolicy;
    XFA_Attribute<QString> m_id;
    XFA_Attribute<QString> m_passwordChar;
    XFA_Attribute<QString> m_use;
    XFA_Attribute<QString> m_usehref;

    /* subnodes */
    XFA_Node<XFA_border> m_border;
    XFA_Node<XFA_extras> m_extras;
    XFA_Node<XFA_margin> m_margin;
};

class XFA_pattern : public XFA_BaseNode
{
public:

    const QString* getId() const {  return m_id.getValue(); }
    const TYPE2* getType() const {  return m_type.getValue(); }
    const QString* getUse() const {  return m_use.getValue(); }
    const QString* getUsehref() const {  return m_usehref.getValue(); }

    const XFA_color* getColor() const {  return m_color.getValue(); }
    const XFA_extras* getExtras() const {  return m_extras.getValue(); }

private:
    /* properties */
    XFA_Attribute<QString> m_id;
    XFA_Attribute<TYPE2> m_type;
    XFA_Attribute<QString> m_use;
    XFA_Attribute<QString> m_usehref;

    /* subnodes */
    XFA_Node<XFA_color> m_color;
    XFA_Node<XFA_extras> m_extras;
};

class XFA_picture : public XFA_BaseNode
{
public:

    const QString* getId() const {  return m_id.getValue(); }
    const QString* getUse() const {  return m_use.getValue(); }
    const QString* getUsehref() const {  return m_usehref.getValue(); }


    const QString* getNodeValue() const {  return m_nodeValue.getValue(); }

private:
    /* properties */
    XFA_Attribute<QString> m_id;
    XFA_Attribute<QString> m_use;
    XFA_Attribute<QString> m_usehref;

    /* subnodes */

    XFA_Value<QString> m_nodeValue;
};

class XFA_proto : public XFA_BaseNode
{
public:


    const std::vector<XFA_Node<XFA_appearanceFilter>>& getAppearanceFilter() const {  return m_appearanceFilter; }
    const std::vector<XFA_Node<XFA_arc>>& getArc() const {  return m_arc; }
    const std::vector<XFA_Node<XFA_area>>& getArea() const {  return m_area; }
    const std::vector<XFA_Node<XFA_assist>>& getAssist() const {  return m_assist; }
    const std::vector<XFA_Node<XFA_barcode>>& getBarcode() const {  return m_barcode; }
    const std::vector<XFA_Node<XFA_bindItems>>& getBindItems() const {  return m_bindItems; }
    const std::vector<XFA_Node<XFA_bookend>>& getBookend() const {  return m_bookend; }
    const std::vector<XFA_Node<XFA_boolean>>& getBoolean() const {  return m_boolean; }
    const std::vector<XFA_Node<XFA_border>>& getBorder() const {  return m_border; }
    const std::vector<XFA_Node<XFA_break>>& getBreak() const {  return m_break; }
    const std::vector<XFA_Node<XFA_breakAfter>>& getBreakAfter() const {  return m_breakAfter; }
    const std::vector<XFA_Node<XFA_breakBefore>>& getBreakBefore() const {  return m_breakBefore; }
    const std::vector<XFA_Node<XFA_button>>& getButton() const {  return m_button; }
    const std::vector<XFA_Node<XFA_calculate>>& getCalculate() const {  return m_calculate; }
    const std::vector<XFA_Node<XFA_caption>>& getCaption() const {  return m_caption; }
    const std::vector<XFA_Node<XFA_certificate>>& getCertificate() const {  return m_certificate; }
    const std::vector<XFA_Node<XFA_certificates>>& getCertificates() const {  return m_certificates; }
    const std::vector<XFA_Node<XFA_checkButton>>& getCheckButton() const {  return m_checkButton; }
    const std::vector<XFA_Node<XFA_choiceList>>& getChoiceList() const {  return m_choiceList; }
    const std::vector<XFA_Node<XFA_color>>& getColor() const {  return m_color; }
    const std::vector<XFA_Node<XFA_comb>>& getComb() const {  return m_comb; }
    const std::vector<XFA_Node<XFA_connect>>& getConnect() const {  return m_connect; }
    const std::vector<XFA_Node<XFA_contentArea>>& getContentArea() const {  return m_contentArea; }
    const std::vector<XFA_Node<XFA_corner>>& getCorner() const {  return m_corner; }
    const std::vector<XFA_Node<XFA_date>>& getDate() const {  return m_date; }
    const std::vector<XFA_Node<XFA_dateTime>>& getDateTime() const {  return m_dateTime; }
    const std::vector<XFA_Node<XFA_dateTimeEdit>>& getDateTimeEdit() const {  return m_dateTimeEdit; }
    const std::vector<XFA_Node<XFA_decimal>>& getDecimal() const {  return m_decimal; }
    const std::vector<XFA_Node<XFA_defaultUi>>& getDefaultUi() const {  return m_defaultUi; }
    const std::vector<XFA_Node<XFA_desc>>& getDesc() const {  return m_desc; }
    const std::vector<XFA_Node<XFA_digestMethod>>& getDigestMethod() const {  return m_digestMethod; }
    const std::vector<XFA_Node<XFA_digestMethods>>& getDigestMethods() const {  return m_digestMethods; }
    const std::vector<XFA_Node<XFA_draw>>& getDraw() const {  return m_draw; }
    const std::vector<XFA_Node<XFA_edge>>& getEdge() const {  return m_edge; }
    const std::vector<XFA_Node<XFA_encoding>>& getEncoding() const {  return m_encoding; }
    const std::vector<XFA_Node<XFA_encodings>>& getEncodings() const {  return m_encodings; }
    const std::vector<XFA_Node<XFA_encrypt>>& getEncrypt() const {  return m_encrypt; }
    const std::vector<XFA_Node<XFA_encryptData>>& getEncryptData() const {  return m_encryptData; }
    const std::vector<XFA_Node<XFA_encryption>>& getEncryption() const {  return m_encryption; }
    const std::vector<XFA_Node<XFA_encryptionMethod>>& getEncryptionMethod() const {  return m_encryptionMethod; }
    const std::vector<XFA_Node<XFA_encryptionMethods>>& getEncryptionMethods() const {  return m_encryptionMethods; }
    const std::vector<XFA_Node<XFA_event>>& getEvent() const {  return m_event; }
    const std::vector<XFA_Node<XFA_exData>>& getExData() const {  return m_exData; }
    const std::vector<XFA_Node<XFA_exObject>>& getExObject() const {  return m_exObject; }
    const std::vector<XFA_Node<XFA_exclGroup>>& getExclGroup() const {  return m_exclGroup; }
    const std::vector<XFA_Node<XFA_execute>>& getExecute() const {  return m_execute; }
    const std::vector<XFA_Node<XFA_extras>>& getExtras() const {  return m_extras; }
    const std::vector<XFA_Node<XFA_field>>& getField() const {  return m_field; }
    const std::vector<XFA_Node<XFA_fill>>& getFill() const {  return m_fill; }
    const std::vector<XFA_Node<XFA_filter>>& getFilter() const {  return m_filter; }
    const std::vector<XFA_Node<XFA_float>>& getFloat() const {  return m_float; }
    const std::vector<XFA_Node<XFA_font>>& getFont() const {  return m_font; }
    const std::vector<XFA_Node<XFA_format>>& getFormat() const {  return m_format; }
    const std::vector<XFA_Node<XFA_handler>>& getHandler() const {  return m_handler; }
    const std::vector<XFA_Node<XFA_hyphenation>>& getHyphenation() const {  return m_hyphenation; }
    const std::vector<XFA_Node<XFA_image>>& getImage() const {  return m_image; }
    const std::vector<XFA_Node<XFA_imageEdit>>& getImageEdit() const {  return m_imageEdit; }
    const std::vector<XFA_Node<XFA_integer>>& getInteger() const {  return m_integer; }
    const std::vector<XFA_Node<XFA_issuers>>& getIssuers() const {  return m_issuers; }
    const std::vector<XFA_Node<XFA_items>>& getItems() const {  return m_items; }
    const std::vector<XFA_Node<XFA_keep>>& getKeep() const {  return m_keep; }
    const std::vector<XFA_Node<XFA_keyUsage>>& getKeyUsage() const {  return m_keyUsage; }
    const std::vector<XFA_Node<XFA_line>>& getLine() const {  return m_line; }
    const std::vector<XFA_Node<XFA_linear>>& getLinear() const {  return m_linear; }
    const std::vector<XFA_Node<XFA_lockDocument>>& getLockDocument() const {  return m_lockDocument; }
    const std::vector<XFA_Node<XFA_manifest>>& getManifest() const {  return m_manifest; }
    const std::vector<XFA_Node<XFA_margin>>& getMargin() const {  return m_margin; }
    const std::vector<XFA_Node<XFA_mdp>>& getMdp() const {  return m_mdp; }
    const std::vector<XFA_Node<XFA_medium>>& getMedium() const {  return m_medium; }
    const std::vector<XFA_Node<XFA_message>>& getMessage() const {  return m_message; }
    const std::vector<XFA_Node<XFA_numericEdit>>& getNumericEdit() const {  return m_numericEdit; }
    const std::vector<XFA_Node<XFA_occur>>& getOccur() const {  return m_occur; }
    const std::vector<XFA_Node<XFA_oid>>& getOid() const {  return m_oid; }
    const std::vector<XFA_Node<XFA_oids>>& getOids() const {  return m_oids; }
    const std::vector<XFA_Node<XFA_overflow>>& getOverflow() const {  return m_overflow; }
    const std::vector<XFA_Node<XFA_pageArea>>& getPageArea() const {  return m_pageArea; }
    const std::vector<XFA_Node<XFA_pageSet>>& getPageSet() const {  return m_pageSet; }
    const std::vector<XFA_Node<XFA_para>>& getPara() const {  return m_para; }
    const std::vector<XFA_Node<XFA_passwordEdit>>& getPasswordEdit() const {  return m_passwordEdit; }
    const std::vector<XFA_Node<XFA_pattern>>& getPattern() const {  return m_pattern; }
    const std::vector<XFA_Node<XFA_picture>>& getPicture() const {  return m_picture; }
    const std::vector<XFA_Node<XFA_radial>>& getRadial() const {  return m_radial; }
    const std::vector<XFA_Node<XFA_reason>>& getReason() const {  return m_reason; }
    const std::vector<XFA_Node<XFA_reasons>>& getReasons() const {  return m_reasons; }
    const std::vector<XFA_Node<XFA_rectangle>>& getRectangle() const {  return m_rectangle; }
    const std::vector<XFA_Node<XFA_ref>>& getRef() const {  return m_ref; }
    const std::vector<XFA_Node<XFA_script>>& getScript() const {  return m_script; }
    const std::vector<XFA_Node<XFA_setProperty>>& getSetProperty() const {  return m_setProperty; }
    const std::vector<XFA_Node<XFA_signData>>& getSignData() const {  return m_signData; }
    const std::vector<XFA_Node<XFA_signature>>& getSignature() const {  return m_signature; }
    const std::vector<XFA_Node<XFA_signing>>& getSigning() const {  return m_signing; }
    const std::vector<XFA_Node<XFA_solid>>& getSolid() const {  return m_solid; }
    const std::vector<XFA_Node<XFA_speak>>& getSpeak() const {  return m_speak; }
    const std::vector<XFA_Node<XFA_stipple>>& getStipple() const {  return m_stipple; }
    const std::vector<XFA_Node<XFA_subform>>& getSubform() const {  return m_subform; }
    const std::vector<XFA_Node<XFA_subformSet>>& getSubformSet() const {  return m_subformSet; }
    const std::vector<XFA_Node<XFA_subjectDN>>& getSubjectDN() const {  return m_subjectDN; }
    const std::vector<XFA_Node<XFA_subjectDNs>>& getSubjectDNs() const {  return m_subjectDNs; }
    const std::vector<XFA_Node<XFA_submit>>& getSubmit() const {  return m_submit; }
    const std::vector<XFA_Node<XFA_text>>& getText() const {  return m_text; }
    const std::vector<XFA_Node<XFA_textEdit>>& getTextEdit() const {  return m_textEdit; }
    const std::vector<XFA_Node<XFA_time>>& getTime() const {  return m_time; }
    const std::vector<XFA_Node<XFA_timeStamp>>& getTimeStamp() const {  return m_timeStamp; }
    const std::vector<XFA_Node<XFA_toolTip>>& getToolTip() const {  return m_toolTip; }
    const std::vector<XFA_Node<XFA_traversal>>& getTraversal() const {  return m_traversal; }
    const std::vector<XFA_Node<XFA_traverse>>& getTraverse() const {  return m_traverse; }
    const std::vector<XFA_Node<XFA_ui>>& getUi() const {  return m_ui; }
    const std::vector<XFA_Node<XFA_validate>>& getValidate() const {  return m_validate; }
    const std::vector<XFA_Node<XFA_value>>& getValue() const {  return m_value; }
    const std::vector<XFA_Node<XFA_variables>>& getVariables() const {  return m_variables; }

private:
    /* properties */

    /* subnodes */
    std::vector<XFA_Node<XFA_appearanceFilter>> m_appearanceFilter;
    std::vector<XFA_Node<XFA_arc>> m_arc;
    std::vector<XFA_Node<XFA_area>> m_area;
    std::vector<XFA_Node<XFA_assist>> m_assist;
    std::vector<XFA_Node<XFA_barcode>> m_barcode;
    std::vector<XFA_Node<XFA_bindItems>> m_bindItems;
    std::vector<XFA_Node<XFA_bookend>> m_bookend;
    std::vector<XFA_Node<XFA_boolean>> m_boolean;
    std::vector<XFA_Node<XFA_border>> m_border;
    std::vector<XFA_Node<XFA_break>> m_break;
    std::vector<XFA_Node<XFA_breakAfter>> m_breakAfter;
    std::vector<XFA_Node<XFA_breakBefore>> m_breakBefore;
    std::vector<XFA_Node<XFA_button>> m_button;
    std::vector<XFA_Node<XFA_calculate>> m_calculate;
    std::vector<XFA_Node<XFA_caption>> m_caption;
    std::vector<XFA_Node<XFA_certificate>> m_certificate;
    std::vector<XFA_Node<XFA_certificates>> m_certificates;
    std::vector<XFA_Node<XFA_checkButton>> m_checkButton;
    std::vector<XFA_Node<XFA_choiceList>> m_choiceList;
    std::vector<XFA_Node<XFA_color>> m_color;
    std::vector<XFA_Node<XFA_comb>> m_comb;
    std::vector<XFA_Node<XFA_connect>> m_connect;
    std::vector<XFA_Node<XFA_contentArea>> m_contentArea;
    std::vector<XFA_Node<XFA_corner>> m_corner;
    std::vector<XFA_Node<XFA_date>> m_date;
    std::vector<XFA_Node<XFA_dateTime>> m_dateTime;
    std::vector<XFA_Node<XFA_dateTimeEdit>> m_dateTimeEdit;
    std::vector<XFA_Node<XFA_decimal>> m_decimal;
    std::vector<XFA_Node<XFA_defaultUi>> m_defaultUi;
    std::vector<XFA_Node<XFA_desc>> m_desc;
    std::vector<XFA_Node<XFA_digestMethod>> m_digestMethod;
    std::vector<XFA_Node<XFA_digestMethods>> m_digestMethods;
    std::vector<XFA_Node<XFA_draw>> m_draw;
    std::vector<XFA_Node<XFA_edge>> m_edge;
    std::vector<XFA_Node<XFA_encoding>> m_encoding;
    std::vector<XFA_Node<XFA_encodings>> m_encodings;
    std::vector<XFA_Node<XFA_encrypt>> m_encrypt;
    std::vector<XFA_Node<XFA_encryptData>> m_encryptData;
    std::vector<XFA_Node<XFA_encryption>> m_encryption;
    std::vector<XFA_Node<XFA_encryptionMethod>> m_encryptionMethod;
    std::vector<XFA_Node<XFA_encryptionMethods>> m_encryptionMethods;
    std::vector<XFA_Node<XFA_event>> m_event;
    std::vector<XFA_Node<XFA_exData>> m_exData;
    std::vector<XFA_Node<XFA_exObject>> m_exObject;
    std::vector<XFA_Node<XFA_exclGroup>> m_exclGroup;
    std::vector<XFA_Node<XFA_execute>> m_execute;
    std::vector<XFA_Node<XFA_extras>> m_extras;
    std::vector<XFA_Node<XFA_field>> m_field;
    std::vector<XFA_Node<XFA_fill>> m_fill;
    std::vector<XFA_Node<XFA_filter>> m_filter;
    std::vector<XFA_Node<XFA_float>> m_float;
    std::vector<XFA_Node<XFA_font>> m_font;
    std::vector<XFA_Node<XFA_format>> m_format;
    std::vector<XFA_Node<XFA_handler>> m_handler;
    std::vector<XFA_Node<XFA_hyphenation>> m_hyphenation;
    std::vector<XFA_Node<XFA_image>> m_image;
    std::vector<XFA_Node<XFA_imageEdit>> m_imageEdit;
    std::vector<XFA_Node<XFA_integer>> m_integer;
    std::vector<XFA_Node<XFA_issuers>> m_issuers;
    std::vector<XFA_Node<XFA_items>> m_items;
    std::vector<XFA_Node<XFA_keep>> m_keep;
    std::vector<XFA_Node<XFA_keyUsage>> m_keyUsage;
    std::vector<XFA_Node<XFA_line>> m_line;
    std::vector<XFA_Node<XFA_linear>> m_linear;
    std::vector<XFA_Node<XFA_lockDocument>> m_lockDocument;
    std::vector<XFA_Node<XFA_manifest>> m_manifest;
    std::vector<XFA_Node<XFA_margin>> m_margin;
    std::vector<XFA_Node<XFA_mdp>> m_mdp;
    std::vector<XFA_Node<XFA_medium>> m_medium;
    std::vector<XFA_Node<XFA_message>> m_message;
    std::vector<XFA_Node<XFA_numericEdit>> m_numericEdit;
    std::vector<XFA_Node<XFA_occur>> m_occur;
    std::vector<XFA_Node<XFA_oid>> m_oid;
    std::vector<XFA_Node<XFA_oids>> m_oids;
    std::vector<XFA_Node<XFA_overflow>> m_overflow;
    std::vector<XFA_Node<XFA_pageArea>> m_pageArea;
    std::vector<XFA_Node<XFA_pageSet>> m_pageSet;
    std::vector<XFA_Node<XFA_para>> m_para;
    std::vector<XFA_Node<XFA_passwordEdit>> m_passwordEdit;
    std::vector<XFA_Node<XFA_pattern>> m_pattern;
    std::vector<XFA_Node<XFA_picture>> m_picture;
    std::vector<XFA_Node<XFA_radial>> m_radial;
    std::vector<XFA_Node<XFA_reason>> m_reason;
    std::vector<XFA_Node<XFA_reasons>> m_reasons;
    std::vector<XFA_Node<XFA_rectangle>> m_rectangle;
    std::vector<XFA_Node<XFA_ref>> m_ref;
    std::vector<XFA_Node<XFA_script>> m_script;
    std::vector<XFA_Node<XFA_setProperty>> m_setProperty;
    std::vector<XFA_Node<XFA_signData>> m_signData;
    std::vector<XFA_Node<XFA_signature>> m_signature;
    std::vector<XFA_Node<XFA_signing>> m_signing;
    std::vector<XFA_Node<XFA_solid>> m_solid;
    std::vector<XFA_Node<XFA_speak>> m_speak;
    std::vector<XFA_Node<XFA_stipple>> m_stipple;
    std::vector<XFA_Node<XFA_subform>> m_subform;
    std::vector<XFA_Node<XFA_subformSet>> m_subformSet;
    std::vector<XFA_Node<XFA_subjectDN>> m_subjectDN;
    std::vector<XFA_Node<XFA_subjectDNs>> m_subjectDNs;
    std::vector<XFA_Node<XFA_submit>> m_submit;
    std::vector<XFA_Node<XFA_text>> m_text;
    std::vector<XFA_Node<XFA_textEdit>> m_textEdit;
    std::vector<XFA_Node<XFA_time>> m_time;
    std::vector<XFA_Node<XFA_timeStamp>> m_timeStamp;
    std::vector<XFA_Node<XFA_toolTip>> m_toolTip;
    std::vector<XFA_Node<XFA_traversal>> m_traversal;
    std::vector<XFA_Node<XFA_traverse>> m_traverse;
    std::vector<XFA_Node<XFA_ui>> m_ui;
    std::vector<XFA_Node<XFA_validate>> m_validate;
    std::vector<XFA_Node<XFA_value>> m_value;
    std::vector<XFA_Node<XFA_variables>> m_variables;
};

class XFA_radial : public XFA_BaseNode
{
public:

    const QString* getId() const {  return m_id.getValue(); }
    const TYPE3* getType() const {  return m_type.getValue(); }
    const QString* getUse() const {  return m_use.getValue(); }
    const QString* getUsehref() const {  return m_usehref.getValue(); }

    const XFA_color* getColor() const {  return m_color.getValue(); }
    const XFA_extras* getExtras() const {  return m_extras.getValue(); }

private:
    /* properties */
    XFA_Attribute<QString> m_id;
    XFA_Attribute<TYPE3> m_type;
    XFA_Attribute<QString> m_use;
    XFA_Attribute<QString> m_usehref;

    /* subnodes */
    XFA_Node<XFA_color> m_color;
    XFA_Node<XFA_extras> m_extras;
};

class XFA_reason : public XFA_BaseNode
{
public:

    const QString* getId() const {  return m_id.getValue(); }
    const QString* getName() const {  return m_name.getValue(); }
    const QString* getUse() const {  return m_use.getValue(); }
    const QString* getUsehref() const {  return m_usehref.getValue(); }


    const QString* getNodeValue() const {  return m_nodeValue.getValue(); }

private:
    /* properties */
    XFA_Attribute<QString> m_id;
    XFA_Attribute<QString> m_name;
    XFA_Attribute<QString> m_use;
    XFA_Attribute<QString> m_usehref;

    /* subnodes */

    XFA_Value<QString> m_nodeValue;
};

class XFA_reasons : public XFA_BaseNode
{
public:

    const QString* getId() const {  return m_id.getValue(); }
    const TYPE* getType() const {  return m_type.getValue(); }
    const QString* getUse() const {  return m_use.getValue(); }
    const QString* getUsehref() const {  return m_usehref.getValue(); }

    const std::vector<XFA_Node<XFA_reason>>& getReason() const {  return m_reason; }

private:
    /* properties */
    XFA_Attribute<QString> m_id;
    XFA_Attribute<TYPE> m_type;
    XFA_Attribute<QString> m_use;
    XFA_Attribute<QString> m_usehref;

    /* subnodes */
    std::vector<XFA_Node<XFA_reason>> m_reason;
};

class XFA_rectangle : public XFA_BaseNode
{
public:

    const HAND* getHand() const {  return m_hand.getValue(); }
    const QString* getId() const {  return m_id.getValue(); }
    const QString* getUse() const {  return m_use.getValue(); }
    const QString* getUsehref() const {  return m_usehref.getValue(); }

    const std::vector<XFA_Node<XFA_corner>>& getCorner() const {  return m_corner; }
    const std::vector<XFA_Node<XFA_edge>>& getEdge() const {  return m_edge; }
    const XFA_fill* getFill() const {  return m_fill.getValue(); }

private:
    /* properties */
    XFA_Attribute<HAND> m_hand;
    XFA_Attribute<QString> m_id;
    XFA_Attribute<QString> m_use;
    XFA_Attribute<QString> m_usehref;

    /* subnodes */
    std::vector<XFA_Node<XFA_corner>> m_corner;
    std::vector<XFA_Node<XFA_edge>> m_edge;
    XFA_Node<XFA_fill> m_fill;
};

class XFA_ref : public XFA_BaseNode
{
public:

    const QString* getId() const {  return m_id.getValue(); }
    const QString* getUse() const {  return m_use.getValue(); }
    const QString* getUsehref() const {  return m_usehref.getValue(); }


    const QString* getNodeValue() const {  return m_nodeValue.getValue(); }

private:
    /* properties */
    XFA_Attribute<QString> m_id;
    XFA_Attribute<QString> m_use;
    XFA_Attribute<QString> m_usehref;

    /* subnodes */

    XFA_Value<QString> m_nodeValue;
};

class XFA_script : public XFA_BaseNode
{
public:

    const QString* getBinding() const {  return m_binding.getValue(); }
    const QString* getContentType() const {  return m_contentType.getValue(); }
    const QString* getId() const {  return m_id.getValue(); }
    const QString* getName() const {  return m_name.getValue(); }
    const RUNAT* getRunAt() const {  return m_runAt.getValue(); }
    const QString* getUse() const {  return m_use.getValue(); }
    const QString* getUsehref() const {  return m_usehref.getValue(); }


    const QString* getNodeValue() const {  return m_nodeValue.getValue(); }

private:
    /* properties */
    XFA_Attribute<QString> m_binding;
    XFA_Attribute<QString> m_contentType;
    XFA_Attribute<QString> m_id;
    XFA_Attribute<QString> m_name;
    XFA_Attribute<RUNAT> m_runAt;
    XFA_Attribute<QString> m_use;
    XFA_Attribute<QString> m_usehref;

    /* subnodes */

    XFA_Value<QString> m_nodeValue;
};

class XFA_setProperty : public XFA_BaseNode
{
public:

    const QString* getConnection() const {  return m_connection.getValue(); }
    const QString* getRef() const {  return m_ref.getValue(); }
    const QString* getTarget() const {  return m_target.getValue(); }


private:
    /* properties */
    XFA_Attribute<QString> m_connection;
    XFA_Attribute<QString> m_ref;
    XFA_Attribute<QString> m_target;

    /* subnodes */
};

class XFA_signData : public XFA_BaseNode
{
public:

    const QString* getId() const {  return m_id.getValue(); }
    const OPERATION1* getOperation() const {  return m_operation.getValue(); }
    const QString* getRef() const {  return m_ref.getValue(); }
    const QString* getTarget() const {  return m_target.getValue(); }
    const QString* getUse() const {  return m_use.getValue(); }
    const QString* getUsehref() const {  return m_usehref.getValue(); }

    const XFA_filter* getFilter() const {  return m_filter.getValue(); }
    const XFA_manifest* getManifest() const {  return m_manifest.getValue(); }

private:
    /* properties */
    XFA_Attribute<QString> m_id;
    XFA_Attribute<OPERATION1> m_operation;
    XFA_Attribute<QString> m_ref;
    XFA_Attribute<QString> m_target;
    XFA_Attribute<QString> m_use;
    XFA_Attribute<QString> m_usehref;

    /* subnodes */
    XFA_Node<XFA_filter> m_filter;
    XFA_Node<XFA_manifest> m_manifest;
};

class XFA_signature : public XFA_BaseNode
{
public:

    const QString* getId() const {  return m_id.getValue(); }
    const TYPE4* getType() const {  return m_type.getValue(); }
    const QString* getUse() const {  return m_use.getValue(); }
    const QString* getUsehref() const {  return m_usehref.getValue(); }

    const XFA_border* getBorder() const {  return m_border.getValue(); }
    const XFA_extras* getExtras() const {  return m_extras.getValue(); }
    const XFA_filter* getFilter() const {  return m_filter.getValue(); }
    const XFA_manifest* getManifest() const {  return m_manifest.getValue(); }
    const XFA_margin* getMargin() const {  return m_margin.getValue(); }

private:
    /* properties */
    XFA_Attribute<QString> m_id;
    XFA_Attribute<TYPE4> m_type;
    XFA_Attribute<QString> m_use;
    XFA_Attribute<QString> m_usehref;

    /* subnodes */
    XFA_Node<XFA_border> m_border;
    XFA_Node<XFA_extras> m_extras;
    XFA_Node<XFA_filter> m_filter;
    XFA_Node<XFA_manifest> m_manifest;
    XFA_Node<XFA_margin> m_margin;
};

class XFA_signing : public XFA_BaseNode
{
public:

    const QString* getId() const {  return m_id.getValue(); }
    const TYPE* getType() const {  return m_type.getValue(); }
    const QString* getUse() const {  return m_use.getValue(); }
    const QString* getUsehref() const {  return m_usehref.getValue(); }

    const std::vector<XFA_Node<XFA_certificate>>& getCertificate() const {  return m_certificate; }

private:
    /* properties */
    XFA_Attribute<QString> m_id;
    XFA_Attribute<TYPE> m_type;
    XFA_Attribute<QString> m_use;
    XFA_Attribute<QString> m_usehref;

    /* subnodes */
    std::vector<XFA_Node<XFA_certificate>> m_certificate;
};

class XFA_solid : public XFA_BaseNode
{
public:

    const QString* getId() const {  return m_id.getValue(); }
    const QString* getUse() const {  return m_use.getValue(); }
    const QString* getUsehref() const {  return m_usehref.getValue(); }

    const XFA_extras* getExtras() const {  return m_extras.getValue(); }

private:
    /* properties */
    XFA_Attribute<QString> m_id;
    XFA_Attribute<QString> m_use;
    XFA_Attribute<QString> m_usehref;

    /* subnodes */
    XFA_Node<XFA_extras> m_extras;
};

class XFA_speak : public XFA_BaseNode
{
public:

    const bool* getDisable() const {  return m_disable.getValue(); }
    const QString* getId() const {  return m_id.getValue(); }
    const PRIORITY* getPriority() const {  return m_priority.getValue(); }
    const QString* getRid() const {  return m_rid.getValue(); }
    const QString* getUse() const {  return m_use.getValue(); }
    const QString* getUsehref() const {  return m_usehref.getValue(); }


    const QString* getNodeValue() const {  return m_nodeValue.getValue(); }

private:
    /* properties */
    XFA_Attribute<bool> m_disable;
    XFA_Attribute<QString> m_id;
    XFA_Attribute<PRIORITY> m_priority;
    XFA_Attribute<QString> m_rid;
    XFA_Attribute<QString> m_use;
    XFA_Attribute<QString> m_usehref;

    /* subnodes */

    XFA_Value<QString> m_nodeValue;
};

class XFA_stipple : public XFA_BaseNode
{
public:

    const QString* getId() const {  return m_id.getValue(); }
    const PDFInteger* getRate() const {  return m_rate.getValue(); }
    const QString* getUse() const {  return m_use.getValue(); }
    const QString* getUsehref() const {  return m_usehref.getValue(); }

    const XFA_color* getColor() const {  return m_color.getValue(); }
    const XFA_extras* getExtras() const {  return m_extras.getValue(); }

private:
    /* properties */
    XFA_Attribute<QString> m_id;
    XFA_Attribute<PDFInteger> m_rate;
    XFA_Attribute<QString> m_use;
    XFA_Attribute<QString> m_usehref;

    /* subnodes */
    XFA_Node<XFA_color> m_color;
    XFA_Node<XFA_extras> m_extras;
};

class XFA_subform : public XFA_BaseNode
{
public:

    const ACCESS* getAccess() const {  return m_access.getValue(); }
    const bool* getAllowMacro() const {  return m_allowMacro.getValue(); }
    const ANCHORTYPE* getAnchorType() const {  return m_anchorType.getValue(); }
    const PDFInteger* getColSpan() const {  return m_colSpan.getValue(); }
    const QString* getColumnWidths() const {  return m_columnWidths.getValue(); }
    const XFA_Measurement* getH() const {  return m_h.getValue(); }
    const HALIGN* getHAlign() const {  return m_hAlign.getValue(); }
    const QString* getId() const {  return m_id.getValue(); }
    const LAYOUT* getLayout() const {  return m_layout.getValue(); }
    const QString* getLocale() const {  return m_locale.getValue(); }
    const XFA_Measurement* getMaxH() const {  return m_maxH.getValue(); }
    const XFA_Measurement* getMaxW() const {  return m_maxW.getValue(); }
    const MERGEMODE* getMergeMode() const {  return m_mergeMode.getValue(); }
    const XFA_Measurement* getMinH() const {  return m_minH.getValue(); }
    const XFA_Measurement* getMinW() const {  return m_minW.getValue(); }
    const QString* getName() const {  return m_name.getValue(); }
    const PRESENCE* getPresence() const {  return m_presence.getValue(); }
    const QString* getRelevant() const {  return m_relevant.getValue(); }
    const RESTORESTATE* getRestoreState() const {  return m_restoreState.getValue(); }
    const SCOPE* getScope() const {  return m_scope.getValue(); }
    const QString* getUse() const {  return m_use.getValue(); }
    const QString* getUsehref() const {  return m_usehref.getValue(); }
    const XFA_Measurement* getW() const {  return m_w.getValue(); }
    const XFA_Measurement* getX() const {  return m_x.getValue(); }
    const XFA_Measurement* getY() const {  return m_y.getValue(); }

    const XFA_assist* getAssist() const {  return m_assist.getValue(); }
    const XFA_bind* getBind() const {  return m_bind.getValue(); }
    const XFA_bookend* getBookend() const {  return m_bookend.getValue(); }
    const XFA_border* getBorder() const {  return m_border.getValue(); }
    const XFA_break* getBreak() const {  return m_break.getValue(); }
    const XFA_calculate* getCalculate() const {  return m_calculate.getValue(); }
    const XFA_desc* getDesc() const {  return m_desc.getValue(); }
    const XFA_extras* getExtras() const {  return m_extras.getValue(); }
    const XFA_keep* getKeep() const {  return m_keep.getValue(); }
    const XFA_margin* getMargin() const {  return m_margin.getValue(); }
    const XFA_occur* getOccur() const {  return m_occur.getValue(); }
    const XFA_overflow* getOverflow() const {  return m_overflow.getValue(); }
    const XFA_pageSet* getPageSet() const {  return m_pageSet.getValue(); }
    const XFA_para* getPara() const {  return m_para.getValue(); }
    const XFA_traversal* getTraversal() const {  return m_traversal.getValue(); }
    const XFA_validate* getValidate() const {  return m_validate.getValue(); }
    const XFA_variables* getVariables() const {  return m_variables.getValue(); }
    const std::vector<XFA_Node<XFA_area>>& getArea() const {  return m_area; }
    const std::vector<XFA_Node<XFA_breakAfter>>& getBreakAfter() const {  return m_breakAfter; }
    const std::vector<XFA_Node<XFA_breakBefore>>& getBreakBefore() const {  return m_breakBefore; }
    const std::vector<XFA_Node<XFA_connect>>& getConnect() const {  return m_connect; }
    const std::vector<XFA_Node<XFA_draw>>& getDraw() const {  return m_draw; }
    const std::vector<XFA_Node<XFA_event>>& getEvent() const {  return m_event; }
    const std::vector<XFA_Node<XFA_exObject>>& getExObject() const {  return m_exObject; }
    const std::vector<XFA_Node<XFA_exclGroup>>& getExclGroup() const {  return m_exclGroup; }
    const std::vector<XFA_Node<XFA_field>>& getField() const {  return m_field; }
    const std::vector<XFA_Node<XFA_proto>>& getProto() const {  return m_proto; }
    const std::vector<XFA_Node<XFA_setProperty>>& getSetProperty() const {  return m_setProperty; }
    const std::vector<XFA_Node<XFA_subform>>& getSubform() const {  return m_subform; }
    const std::vector<XFA_Node<XFA_subformSet>>& getSubformSet() const {  return m_subformSet; }

private:
    /* properties */
    XFA_Attribute<ACCESS> m_access;
    XFA_Attribute<bool> m_allowMacro;
    XFA_Attribute<ANCHORTYPE> m_anchorType;
    XFA_Attribute<PDFInteger> m_colSpan;
    XFA_Attribute<QString> m_columnWidths;
    XFA_Attribute<XFA_Measurement> m_h;
    XFA_Attribute<HALIGN> m_hAlign;
    XFA_Attribute<QString> m_id;
    XFA_Attribute<LAYOUT> m_layout;
    XFA_Attribute<QString> m_locale;
    XFA_Attribute<XFA_Measurement> m_maxH;
    XFA_Attribute<XFA_Measurement> m_maxW;
    XFA_Attribute<MERGEMODE> m_mergeMode;
    XFA_Attribute<XFA_Measurement> m_minH;
    XFA_Attribute<XFA_Measurement> m_minW;
    XFA_Attribute<QString> m_name;
    XFA_Attribute<PRESENCE> m_presence;
    XFA_Attribute<QString> m_relevant;
    XFA_Attribute<RESTORESTATE> m_restoreState;
    XFA_Attribute<SCOPE> m_scope;
    XFA_Attribute<QString> m_use;
    XFA_Attribute<QString> m_usehref;
    XFA_Attribute<XFA_Measurement> m_w;
    XFA_Attribute<XFA_Measurement> m_x;
    XFA_Attribute<XFA_Measurement> m_y;

    /* subnodes */
    XFA_Node<XFA_assist> m_assist;
    XFA_Node<XFA_bind> m_bind;
    XFA_Node<XFA_bookend> m_bookend;
    XFA_Node<XFA_border> m_border;
    XFA_Node<XFA_break> m_break;
    XFA_Node<XFA_calculate> m_calculate;
    XFA_Node<XFA_desc> m_desc;
    XFA_Node<XFA_extras> m_extras;
    XFA_Node<XFA_keep> m_keep;
    XFA_Node<XFA_margin> m_margin;
    XFA_Node<XFA_occur> m_occur;
    XFA_Node<XFA_overflow> m_overflow;
    XFA_Node<XFA_pageSet> m_pageSet;
    XFA_Node<XFA_para> m_para;
    XFA_Node<XFA_traversal> m_traversal;
    XFA_Node<XFA_validate> m_validate;
    XFA_Node<XFA_variables> m_variables;
    std::vector<XFA_Node<XFA_area>> m_area;
    std::vector<XFA_Node<XFA_breakAfter>> m_breakAfter;
    std::vector<XFA_Node<XFA_breakBefore>> m_breakBefore;
    std::vector<XFA_Node<XFA_connect>> m_connect;
    std::vector<XFA_Node<XFA_draw>> m_draw;
    std::vector<XFA_Node<XFA_event>> m_event;
    std::vector<XFA_Node<XFA_exObject>> m_exObject;
    std::vector<XFA_Node<XFA_exclGroup>> m_exclGroup;
    std::vector<XFA_Node<XFA_field>> m_field;
    std::vector<XFA_Node<XFA_proto>> m_proto;
    std::vector<XFA_Node<XFA_setProperty>> m_setProperty;
    std::vector<XFA_Node<XFA_subform>> m_subform;
    std::vector<XFA_Node<XFA_subformSet>> m_subformSet;
};

class XFA_subformSet : public XFA_BaseNode
{
public:

    const QString* getId() const {  return m_id.getValue(); }
    const QString* getName() const {  return m_name.getValue(); }
    const RELATION1* getRelation() const {  return m_relation.getValue(); }
    const QString* getRelevant() const {  return m_relevant.getValue(); }
    const QString* getUse() const {  return m_use.getValue(); }
    const QString* getUsehref() const {  return m_usehref.getValue(); }

    const XFA_bookend* getBookend() const {  return m_bookend.getValue(); }
    const XFA_break* getBreak() const {  return m_break.getValue(); }
    const XFA_desc* getDesc() const {  return m_desc.getValue(); }
    const XFA_extras* getExtras() const {  return m_extras.getValue(); }
    const XFA_occur* getOccur() const {  return m_occur.getValue(); }
    const XFA_overflow* getOverflow() const {  return m_overflow.getValue(); }
    const std::vector<XFA_Node<XFA_breakAfter>>& getBreakAfter() const {  return m_breakAfter; }
    const std::vector<XFA_Node<XFA_breakBefore>>& getBreakBefore() const {  return m_breakBefore; }
    const std::vector<XFA_Node<XFA_subform>>& getSubform() const {  return m_subform; }
    const std::vector<XFA_Node<XFA_subformSet>>& getSubformSet() const {  return m_subformSet; }

private:
    /* properties */
    XFA_Attribute<QString> m_id;
    XFA_Attribute<QString> m_name;
    XFA_Attribute<RELATION1> m_relation;
    XFA_Attribute<QString> m_relevant;
    XFA_Attribute<QString> m_use;
    XFA_Attribute<QString> m_usehref;

    /* subnodes */
    XFA_Node<XFA_bookend> m_bookend;
    XFA_Node<XFA_break> m_break;
    XFA_Node<XFA_desc> m_desc;
    XFA_Node<XFA_extras> m_extras;
    XFA_Node<XFA_occur> m_occur;
    XFA_Node<XFA_overflow> m_overflow;
    std::vector<XFA_Node<XFA_breakAfter>> m_breakAfter;
    std::vector<XFA_Node<XFA_breakBefore>> m_breakBefore;
    std::vector<XFA_Node<XFA_subform>> m_subform;
    std::vector<XFA_Node<XFA_subformSet>> m_subformSet;
};

class XFA_subjectDN : public XFA_BaseNode
{
public:

    const QString* getDelimiter() const {  return m_delimiter.getValue(); }
    const QString* getId() const {  return m_id.getValue(); }
    const QString* getName() const {  return m_name.getValue(); }
    const QString* getUse() const {  return m_use.getValue(); }
    const QString* getUsehref() const {  return m_usehref.getValue(); }


    const QString* getNodeValue() const {  return m_nodeValue.getValue(); }

private:
    /* properties */
    XFA_Attribute<QString> m_delimiter;
    XFA_Attribute<QString> m_id;
    XFA_Attribute<QString> m_name;
    XFA_Attribute<QString> m_use;
    XFA_Attribute<QString> m_usehref;

    /* subnodes */

    XFA_Value<QString> m_nodeValue;
};

class XFA_subjectDNs : public XFA_BaseNode
{
public:

    const QString* getId() const {  return m_id.getValue(); }
    const TYPE* getType() const {  return m_type.getValue(); }
    const QString* getUse() const {  return m_use.getValue(); }
    const QString* getUsehref() const {  return m_usehref.getValue(); }

    const std::vector<XFA_Node<XFA_subjectDN>>& getSubjectDN() const {  return m_subjectDN; }

private:
    /* properties */
    XFA_Attribute<QString> m_id;
    XFA_Attribute<TYPE> m_type;
    XFA_Attribute<QString> m_use;
    XFA_Attribute<QString> m_usehref;

    /* subnodes */
    std::vector<XFA_Node<XFA_subjectDN>> m_subjectDN;
};

class XFA_submit : public XFA_BaseNode
{
public:

    const bool* getEmbedPDF() const {  return m_embedPDF.getValue(); }
    const FORMAT* getFormat() const {  return m_format.getValue(); }
    const QString* getId() const {  return m_id.getValue(); }
    const QString* getTarget() const {  return m_target.getValue(); }
    const QString* getTextEncoding() const {  return m_textEncoding.getValue(); }
    const QString* getUse() const {  return m_use.getValue(); }
    const QString* getUsehref() const {  return m_usehref.getValue(); }
    const QString* getXdpContent() const {  return m_xdpContent.getValue(); }

    const XFA_encrypt* getEncrypt() const {  return m_encrypt.getValue(); }
    const std::vector<XFA_Node<XFA_encryptData>>& getEncryptData() const {  return m_encryptData; }
    const std::vector<XFA_Node<XFA_signData>>& getSignData() const {  return m_signData; }

private:
    /* properties */
    XFA_Attribute<bool> m_embedPDF;
    XFA_Attribute<FORMAT> m_format;
    XFA_Attribute<QString> m_id;
    XFA_Attribute<QString> m_target;
    XFA_Attribute<QString> m_textEncoding;
    XFA_Attribute<QString> m_use;
    XFA_Attribute<QString> m_usehref;
    XFA_Attribute<QString> m_xdpContent;

    /* subnodes */
    XFA_Node<XFA_encrypt> m_encrypt;
    std::vector<XFA_Node<XFA_encryptData>> m_encryptData;
    std::vector<XFA_Node<XFA_signData>> m_signData;
};

class XFA_template : public XFA_BaseNode
{
public:

    const BASEPROFILE* getBaseProfile() const {  return m_baseProfile.getValue(); }

    const XFA_extras* getExtras() const {  return m_extras.getValue(); }
    const std::vector<XFA_Node<XFA_subform>>& getSubform() const {  return m_subform; }

private:
    /* properties */
    XFA_Attribute<BASEPROFILE> m_baseProfile;

    /* subnodes */
    XFA_Node<XFA_extras> m_extras;
    std::vector<XFA_Node<XFA_subform>> m_subform;
};

class XFA_text : public XFA_BaseNode
{
public:

    const QString* getId() const {  return m_id.getValue(); }
    const PDFInteger* getMaxChars() const {  return m_maxChars.getValue(); }
    const QString* getName() const {  return m_name.getValue(); }
    const QString* getRid() const {  return m_rid.getValue(); }
    const QString* getUse() const {  return m_use.getValue(); }
    const QString* getUsehref() const {  return m_usehref.getValue(); }


    const QString* getNodeValue() const {  return m_nodeValue.getValue(); }

private:
    /* properties */
    XFA_Attribute<QString> m_id;
    XFA_Attribute<PDFInteger> m_maxChars;
    XFA_Attribute<QString> m_name;
    XFA_Attribute<QString> m_rid;
    XFA_Attribute<QString> m_use;
    XFA_Attribute<QString> m_usehref;

    /* subnodes */

    XFA_Value<QString> m_nodeValue;
};

class XFA_textEdit : public XFA_BaseNode
{
public:

    const bool* getAllowRichText() const {  return m_allowRichText.getValue(); }
    const HSCROLLPOLICY* getHScrollPolicy() const {  return m_hScrollPolicy.getValue(); }
    const QString* getId() const {  return m_id.getValue(); }
    const MULTILINE* getMultiLine() const {  return m_multiLine.getValue(); }
    const QString* getUse() const {  return m_use.getValue(); }
    const QString* getUsehref() const {  return m_usehref.getValue(); }
    const VSCROLLPOLICY* getVScrollPolicy() const {  return m_vScrollPolicy.getValue(); }

    const XFA_border* getBorder() const {  return m_border.getValue(); }
    const XFA_comb* getComb() const {  return m_comb.getValue(); }
    const XFA_extras* getExtras() const {  return m_extras.getValue(); }
    const XFA_margin* getMargin() const {  return m_margin.getValue(); }

private:
    /* properties */
    XFA_Attribute<bool> m_allowRichText;
    XFA_Attribute<HSCROLLPOLICY> m_hScrollPolicy;
    XFA_Attribute<QString> m_id;
    XFA_Attribute<MULTILINE> m_multiLine;
    XFA_Attribute<QString> m_use;
    XFA_Attribute<QString> m_usehref;
    XFA_Attribute<VSCROLLPOLICY> m_vScrollPolicy;

    /* subnodes */
    XFA_Node<XFA_border> m_border;
    XFA_Node<XFA_comb> m_comb;
    XFA_Node<XFA_extras> m_extras;
    XFA_Node<XFA_margin> m_margin;
};

class XFA_time : public XFA_BaseNode
{
public:

    const QString* getId() const {  return m_id.getValue(); }
    const QString* getName() const {  return m_name.getValue(); }
    const QString* getUse() const {  return m_use.getValue(); }
    const QString* getUsehref() const {  return m_usehref.getValue(); }


    const QString* getNodeValue() const {  return m_nodeValue.getValue(); }

private:
    /* properties */
    XFA_Attribute<QString> m_id;
    XFA_Attribute<QString> m_name;
    XFA_Attribute<QString> m_use;
    XFA_Attribute<QString> m_usehref;

    /* subnodes */

    XFA_Value<QString> m_nodeValue;
};

class XFA_timeStamp : public XFA_BaseNode
{
public:

    const QString* getId() const {  return m_id.getValue(); }
    const QString* getServer() const {  return m_server.getValue(); }
    const TYPE* getType() const {  return m_type.getValue(); }
    const QString* getUse() const {  return m_use.getValue(); }
    const QString* getUsehref() const {  return m_usehref.getValue(); }


private:
    /* properties */
    XFA_Attribute<QString> m_id;
    XFA_Attribute<QString> m_server;
    XFA_Attribute<TYPE> m_type;
    XFA_Attribute<QString> m_use;
    XFA_Attribute<QString> m_usehref;

    /* subnodes */
};

class XFA_toolTip : public XFA_BaseNode
{
public:

    const QString* getId() const {  return m_id.getValue(); }
    const QString* getRid() const {  return m_rid.getValue(); }
    const QString* getUse() const {  return m_use.getValue(); }
    const QString* getUsehref() const {  return m_usehref.getValue(); }


    const QString* getNodeValue() const {  return m_nodeValue.getValue(); }

private:
    /* properties */
    XFA_Attribute<QString> m_id;
    XFA_Attribute<QString> m_rid;
    XFA_Attribute<QString> m_use;
    XFA_Attribute<QString> m_usehref;

    /* subnodes */

    XFA_Value<QString> m_nodeValue;
};

class XFA_traversal : public XFA_BaseNode
{
public:

    const QString* getId() const {  return m_id.getValue(); }
    const QString* getUse() const {  return m_use.getValue(); }
    const QString* getUsehref() const {  return m_usehref.getValue(); }

    const XFA_extras* getExtras() const {  return m_extras.getValue(); }
    const std::vector<XFA_Node<XFA_traverse>>& getTraverse() const {  return m_traverse; }

private:
    /* properties */
    XFA_Attribute<QString> m_id;
    XFA_Attribute<QString> m_use;
    XFA_Attribute<QString> m_usehref;

    /* subnodes */
    XFA_Node<XFA_extras> m_extras;
    std::vector<XFA_Node<XFA_traverse>> m_traverse;
};

class XFA_traverse : public XFA_BaseNode
{
public:

    const QString* getId() const {  return m_id.getValue(); }
    const OPERATION2* getOperation() const {  return m_operation.getValue(); }
    const QString* getRef() const {  return m_ref.getValue(); }
    const QString* getUse() const {  return m_use.getValue(); }
    const QString* getUsehref() const {  return m_usehref.getValue(); }

    const XFA_extras* getExtras() const {  return m_extras.getValue(); }
    const XFA_script* getScript() const {  return m_script.getValue(); }

private:
    /* properties */
    XFA_Attribute<QString> m_id;
    XFA_Attribute<OPERATION2> m_operation;
    XFA_Attribute<QString> m_ref;
    XFA_Attribute<QString> m_use;
    XFA_Attribute<QString> m_usehref;

    /* subnodes */
    XFA_Node<XFA_extras> m_extras;
    XFA_Node<XFA_script> m_script;
};

class XFA_ui : public XFA_BaseNode
{
public:

    const QString* getId() const {  return m_id.getValue(); }
    const QString* getUse() const {  return m_use.getValue(); }
    const QString* getUsehref() const {  return m_usehref.getValue(); }

    const XFA_extras* getExtras() const {  return m_extras.getValue(); }
    const XFA_picture* getPicture() const {  return m_picture.getValue(); }
    const XFA_barcode* getBarcode() const {  return m_barcode.getValue(); }
    const XFA_button* getButton() const {  return m_button.getValue(); }
    const XFA_checkButton* getCheckButton() const {  return m_checkButton.getValue(); }
    const XFA_choiceList* getChoiceList() const {  return m_choiceList.getValue(); }
    const XFA_dateTimeEdit* getDateTimeEdit() const {  return m_dateTimeEdit.getValue(); }
    const XFA_defaultUi* getDefaultUi() const {  return m_defaultUi.getValue(); }
    const XFA_imageEdit* getImageEdit() const {  return m_imageEdit.getValue(); }
    const XFA_numericEdit* getNumericEdit() const {  return m_numericEdit.getValue(); }
    const XFA_passwordEdit* getPasswordEdit() const {  return m_passwordEdit.getValue(); }
    const XFA_signature* getSignature() const {  return m_signature.getValue(); }
    const XFA_textEdit* getTextEdit() const {  return m_textEdit.getValue(); }

private:
    /* properties */
    XFA_Attribute<QString> m_id;
    XFA_Attribute<QString> m_use;
    XFA_Attribute<QString> m_usehref;

    /* subnodes */
    XFA_Node<XFA_extras> m_extras;
    XFA_Node<XFA_picture> m_picture;
    XFA_Node<XFA_barcode> m_barcode;
    XFA_Node<XFA_button> m_button;
    XFA_Node<XFA_checkButton> m_checkButton;
    XFA_Node<XFA_choiceList> m_choiceList;
    XFA_Node<XFA_dateTimeEdit> m_dateTimeEdit;
    XFA_Node<XFA_defaultUi> m_defaultUi;
    XFA_Node<XFA_imageEdit> m_imageEdit;
    XFA_Node<XFA_numericEdit> m_numericEdit;
    XFA_Node<XFA_passwordEdit> m_passwordEdit;
    XFA_Node<XFA_signature> m_signature;
    XFA_Node<XFA_textEdit> m_textEdit;
};

class XFA_validate : public XFA_BaseNode
{
public:

    const FORMATTEST* getFormatTest() const {  return m_formatTest.getValue(); }
    const QString* getId() const {  return m_id.getValue(); }
    const NULLTEST* getNullTest() const {  return m_nullTest.getValue(); }
    const SCRIPTTEST* getScriptTest() const {  return m_scriptTest.getValue(); }
    const QString* getUse() const {  return m_use.getValue(); }
    const QString* getUsehref() const {  return m_usehref.getValue(); }

    const XFA_extras* getExtras() const {  return m_extras.getValue(); }
    const XFA_message* getMessage() const {  return m_message.getValue(); }
    const XFA_picture* getPicture() const {  return m_picture.getValue(); }
    const XFA_script* getScript() const {  return m_script.getValue(); }

private:
    /* properties */
    XFA_Attribute<FORMATTEST> m_formatTest;
    XFA_Attribute<QString> m_id;
    XFA_Attribute<NULLTEST> m_nullTest;
    XFA_Attribute<SCRIPTTEST> m_scriptTest;
    XFA_Attribute<QString> m_use;
    XFA_Attribute<QString> m_usehref;

    /* subnodes */
    XFA_Node<XFA_extras> m_extras;
    XFA_Node<XFA_message> m_message;
    XFA_Node<XFA_picture> m_picture;
    XFA_Node<XFA_script> m_script;
};

class XFA_value : public XFA_BaseNode
{
public:

    const QString* getId() const {  return m_id.getValue(); }
    const bool* getOverride() const {  return m_override.getValue(); }
    const QString* getRelevant() const {  return m_relevant.getValue(); }
    const QString* getUse() const {  return m_use.getValue(); }
    const QString* getUsehref() const {  return m_usehref.getValue(); }

    const XFA_arc* getArc() const {  return m_arc.getValue(); }
    const XFA_boolean* getBoolean() const {  return m_boolean.getValue(); }
    const XFA_date* getDate() const {  return m_date.getValue(); }
    const XFA_dateTime* getDateTime() const {  return m_dateTime.getValue(); }
    const XFA_decimal* getDecimal() const {  return m_decimal.getValue(); }
    const XFA_exData* getExData() const {  return m_exData.getValue(); }
    const XFA_float* getFloat() const {  return m_float.getValue(); }
    const XFA_image* getImage() const {  return m_image.getValue(); }
    const XFA_integer* getInteger() const {  return m_integer.getValue(); }
    const XFA_line* getLine() const {  return m_line.getValue(); }
    const XFA_rectangle* getRectangle() const {  return m_rectangle.getValue(); }
    const XFA_text* getText() const {  return m_text.getValue(); }
    const XFA_time* getTime() const {  return m_time.getValue(); }

private:
    /* properties */
    XFA_Attribute<QString> m_id;
    XFA_Attribute<bool> m_override;
    XFA_Attribute<QString> m_relevant;
    XFA_Attribute<QString> m_use;
    XFA_Attribute<QString> m_usehref;

    /* subnodes */
    XFA_Node<XFA_arc> m_arc;
    XFA_Node<XFA_boolean> m_boolean;
    XFA_Node<XFA_date> m_date;
    XFA_Node<XFA_dateTime> m_dateTime;
    XFA_Node<XFA_decimal> m_decimal;
    XFA_Node<XFA_exData> m_exData;
    XFA_Node<XFA_float> m_float;
    XFA_Node<XFA_image> m_image;
    XFA_Node<XFA_integer> m_integer;
    XFA_Node<XFA_line> m_line;
    XFA_Node<XFA_rectangle> m_rectangle;
    XFA_Node<XFA_text> m_text;
    XFA_Node<XFA_time> m_time;
};

class XFA_variables : public XFA_BaseNode
{
public:

    const QString* getId() const {  return m_id.getValue(); }
    const QString* getUse() const {  return m_use.getValue(); }
    const QString* getUsehref() const {  return m_usehref.getValue(); }

    const std::vector<XFA_Node<XFA_boolean>>& getBoolean() const {  return m_boolean; }
    const std::vector<XFA_Node<XFA_date>>& getDate() const {  return m_date; }
    const std::vector<XFA_Node<XFA_dateTime>>& getDateTime() const {  return m_dateTime; }
    const std::vector<XFA_Node<XFA_decimal>>& getDecimal() const {  return m_decimal; }
    const std::vector<XFA_Node<XFA_exData>>& getExData() const {  return m_exData; }
    const std::vector<XFA_Node<XFA_float>>& getFloat() const {  return m_float; }
    const std::vector<XFA_Node<XFA_image>>& getImage() const {  return m_image; }
    const std::vector<XFA_Node<XFA_integer>>& getInteger() const {  return m_integer; }
    const std::vector<XFA_Node<XFA_manifest>>& getManifest() const {  return m_manifest; }
    const std::vector<XFA_Node<XFA_script>>& getScript() const {  return m_script; }
    const std::vector<XFA_Node<XFA_text>>& getText() const {  return m_text; }
    const std::vector<XFA_Node<XFA_time>>& getTime() const {  return m_time; }

private:
    /* properties */
    XFA_Attribute<QString> m_id;
    XFA_Attribute<QString> m_use;
    XFA_Attribute<QString> m_usehref;

    /* subnodes */
    std::vector<XFA_Node<XFA_boolean>> m_boolean;
    std::vector<XFA_Node<XFA_date>> m_date;
    std::vector<XFA_Node<XFA_dateTime>> m_dateTime;
    std::vector<XFA_Node<XFA_decimal>> m_decimal;
    std::vector<XFA_Node<XFA_exData>> m_exData;
    std::vector<XFA_Node<XFA_float>> m_float;
    std::vector<XFA_Node<XFA_image>> m_image;
    std::vector<XFA_Node<XFA_integer>> m_integer;
    std::vector<XFA_Node<XFA_manifest>> m_manifest;
    std::vector<XFA_Node<XFA_script>> m_script;
    std::vector<XFA_Node<XFA_text>> m_text;
    std::vector<XFA_Node<XFA_time>> m_time;
};

} // namespace xfa


/* END GENERATED CODE */

}   // namespace pdf
