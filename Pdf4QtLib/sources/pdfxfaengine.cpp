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
    enum class BEFORE
    {
        Auto,
        ContentArea,
        PageArea,
        PageEven,
        PageOdd,
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
    enum class EXECUTETYPE
    {
        Import,
        Remerge,
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
    enum class OVERRIDE
    {
        Disabled,
        Error,
        Ignore,
        Warning,
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
    enum class PRESENCE
    {
        Visible,
        Hidden,
        Inactive,
        Invisible,
    };
    enum class RUNAT
    {
        Client,
        Both,
        Server,
    };
    enum class SHAPE
    {
        Square,
        Round,
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
    enum class TRANSFERENCODING
    {
        None,
        Base64,
        Package,
    };
    enum class TYPE
    {
        Optional,
        Required,
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
};

class XFA_appearanceFilter : public XFA_BaseNode
{
public:
    const QString* getId() const {  return m_id.getValue(); }
    const TYPE* getType() const {  return m_type.getValue(); }
    const QString* getUse() const {  return m_use.getValue(); }
    const QString* getUsehref() const {  return m_usehref.getValue(); }

private:
    /* properties */
    XFA_Attribute<QString> m_id;
    XFA_Attribute<TYPE> m_type;
    XFA_Attribute<QString> m_use;
    XFA_Attribute<QString> m_usehref;

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

private:
    /* properties */
    XFA_Attribute<bool> m_circular;
    XFA_Attribute<HAND> m_hand;
    XFA_Attribute<QString> m_id;
    XFA_Attribute<PDFReal> m_startAngle;
    XFA_Attribute<PDFReal> m_sweepAngle;
    XFA_Attribute<QString> m_use;
    XFA_Attribute<QString> m_usehref;

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

};

class XFA_assist : public XFA_BaseNode
{
public:
    const QString* getId() const {  return m_id.getValue(); }
    const QString* getRole() const {  return m_role.getValue(); }
    const QString* getUse() const {  return m_use.getValue(); }
    const QString* getUsehref() const {  return m_usehref.getValue(); }

private:
    /* properties */
    XFA_Attribute<QString> m_id;
    XFA_Attribute<QString> m_role;
    XFA_Attribute<QString> m_use;
    XFA_Attribute<QString> m_usehref;

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

};

class XFA_bind : public XFA_BaseNode
{
public:
    const MATCH* getMatch() const {  return m_match.getValue(); }
    const QString* getRef() const {  return m_ref.getValue(); }

private:
    /* properties */
    XFA_Attribute<MATCH> m_match;
    XFA_Attribute<QString> m_ref;

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

};

class XFA_boolean : public XFA_BaseNode
{
public:
    const QString* getId() const {  return m_id.getValue(); }
    const QString* getName() const {  return m_name.getValue(); }
    const QString* getUse() const {  return m_use.getValue(); }
    const QString* getUsehref() const {  return m_usehref.getValue(); }

private:
    /* properties */
    XFA_Attribute<QString> m_id;
    XFA_Attribute<QString> m_name;
    XFA_Attribute<QString> m_use;
    XFA_Attribute<QString> m_usehref;

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

private:
    /* properties */
    XFA_Attribute<BREAK> m_break;
    XFA_Attribute<HAND> m_hand;
    XFA_Attribute<QString> m_id;
    XFA_Attribute<PRESENCE> m_presence;
    XFA_Attribute<QString> m_relevant;
    XFA_Attribute<QString> m_use;
    XFA_Attribute<QString> m_usehref;

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

};

class XFA_button : public XFA_BaseNode
{
public:
    const HIGHLIGHT* getHighlight() const {  return m_highlight.getValue(); }
    const QString* getId() const {  return m_id.getValue(); }
    const QString* getUse() const {  return m_use.getValue(); }
    const QString* getUsehref() const {  return m_usehref.getValue(); }

private:
    /* properties */
    XFA_Attribute<HIGHLIGHT> m_highlight;
    XFA_Attribute<QString> m_id;
    XFA_Attribute<QString> m_use;
    XFA_Attribute<QString> m_usehref;

};

class XFA_calculate : public XFA_BaseNode
{
public:
    const QString* getId() const {  return m_id.getValue(); }
    const OVERRIDE* getOverride() const {  return m_override.getValue(); }
    const QString* getUse() const {  return m_use.getValue(); }
    const QString* getUsehref() const {  return m_usehref.getValue(); }

private:
    /* properties */
    XFA_Attribute<QString> m_id;
    XFA_Attribute<OVERRIDE> m_override;
    XFA_Attribute<QString> m_use;
    XFA_Attribute<QString> m_usehref;

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

private:
    /* properties */
    XFA_Attribute<QString> m_id;
    XFA_Attribute<PLACEMENT> m_placement;
    XFA_Attribute<PRESENCE> m_presence;
    XFA_Attribute<XFA_Measurement> m_reserve;
    XFA_Attribute<QString> m_use;
    XFA_Attribute<QString> m_usehref;

};

class XFA_certificate : public XFA_BaseNode
{
public:
    const QString* getId() const {  return m_id.getValue(); }
    const QString* getName() const {  return m_name.getValue(); }
    const QString* getUse() const {  return m_use.getValue(); }
    const QString* getUsehref() const {  return m_usehref.getValue(); }

private:
    /* properties */
    XFA_Attribute<QString> m_id;
    XFA_Attribute<QString> m_name;
    XFA_Attribute<QString> m_use;
    XFA_Attribute<QString> m_usehref;

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

private:
    /* properties */
    XFA_Attribute<CREDENTIALSERVERPOLICY> m_credentialServerPolicy;
    XFA_Attribute<QString> m_id;
    XFA_Attribute<QString> m_url;
    XFA_Attribute<QString> m_urlPolicy;
    XFA_Attribute<QString> m_use;
    XFA_Attribute<QString> m_usehref;

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

private:
    /* properties */
    XFA_Attribute<QString> m_id;
    XFA_Attribute<MARK> m_mark;
    XFA_Attribute<SHAPE> m_shape;
    XFA_Attribute<XFA_Measurement> m_size;
    XFA_Attribute<QString> m_use;
    XFA_Attribute<QString> m_usehref;

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

private:
    /* properties */
    XFA_Attribute<COMMITON> m_commitOn;
    XFA_Attribute<QString> m_id;
    XFA_Attribute<OPEN> m_open;
    XFA_Attribute<bool> m_textEntry;
    XFA_Attribute<QString> m_use;
    XFA_Attribute<QString> m_usehref;

};

class XFA_color : public XFA_BaseNode
{
public:
    const QString* getCSpace() const {  return m_cSpace.getValue(); }
    const QString* getId() const {  return m_id.getValue(); }
    const QString* getUse() const {  return m_use.getValue(); }
    const QString* getUsehref() const {  return m_usehref.getValue(); }
    const QString* getValue() const {  return m_value.getValue(); }

private:
    /* properties */
    XFA_Attribute<QString> m_cSpace;
    XFA_Attribute<QString> m_id;
    XFA_Attribute<QString> m_use;
    XFA_Attribute<QString> m_usehref;
    XFA_Attribute<QString> m_value;

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

private:
    /* properties */
    XFA_Attribute<QString> m_connection;
    XFA_Attribute<QString> m_id;
    XFA_Attribute<QString> m_ref;
    XFA_Attribute<USAGE> m_usage;
    XFA_Attribute<QString> m_use;
    XFA_Attribute<QString> m_usehref;

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

};

class XFA_date : public XFA_BaseNode
{
public:
    const QString* getId() const {  return m_id.getValue(); }
    const QString* getName() const {  return m_name.getValue(); }
    const QString* getUse() const {  return m_use.getValue(); }
    const QString* getUsehref() const {  return m_usehref.getValue(); }

private:
    /* properties */
    XFA_Attribute<QString> m_id;
    XFA_Attribute<QString> m_name;
    XFA_Attribute<QString> m_use;
    XFA_Attribute<QString> m_usehref;

};

class XFA_dateTime : public XFA_BaseNode
{
public:
    const QString* getId() const {  return m_id.getValue(); }
    const QString* getName() const {  return m_name.getValue(); }
    const QString* getUse() const {  return m_use.getValue(); }
    const QString* getUsehref() const {  return m_usehref.getValue(); }

private:
    /* properties */
    XFA_Attribute<QString> m_id;
    XFA_Attribute<QString> m_name;
    XFA_Attribute<QString> m_use;
    XFA_Attribute<QString> m_usehref;

};

class XFA_dateTimeEdit : public XFA_BaseNode
{
public:
    const HSCROLLPOLICY* getHScrollPolicy() const {  return m_hScrollPolicy.getValue(); }
    const QString* getId() const {  return m_id.getValue(); }
    const PICKER* getPicker() const {  return m_picker.getValue(); }
    const QString* getUse() const {  return m_use.getValue(); }
    const QString* getUsehref() const {  return m_usehref.getValue(); }

private:
    /* properties */
    XFA_Attribute<HSCROLLPOLICY> m_hScrollPolicy;
    XFA_Attribute<QString> m_id;
    XFA_Attribute<PICKER> m_picker;
    XFA_Attribute<QString> m_use;
    XFA_Attribute<QString> m_usehref;

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

private:
    /* properties */
    XFA_Attribute<PDFInteger> m_fracDigits;
    XFA_Attribute<QString> m_id;
    XFA_Attribute<PDFInteger> m_leadDigits;
    XFA_Attribute<QString> m_name;
    XFA_Attribute<QString> m_use;
    XFA_Attribute<QString> m_usehref;

};

class XFA_defaultUi : public XFA_BaseNode
{
public:
    const QString* getId() const {  return m_id.getValue(); }
    const QString* getUse() const {  return m_use.getValue(); }
    const QString* getUsehref() const {  return m_usehref.getValue(); }

private:
    /* properties */
    XFA_Attribute<QString> m_id;
    XFA_Attribute<QString> m_use;
    XFA_Attribute<QString> m_usehref;

};

class XFA_desc : public XFA_BaseNode
{
public:
    const QString* getId() const {  return m_id.getValue(); }
    const QString* getUse() const {  return m_use.getValue(); }
    const QString* getUsehref() const {  return m_usehref.getValue(); }

private:
    /* properties */
    XFA_Attribute<QString> m_id;
    XFA_Attribute<QString> m_use;
    XFA_Attribute<QString> m_usehref;

};

class XFA_digestMethod : public XFA_BaseNode
{
public:
    const QString* getId() const {  return m_id.getValue(); }
    const QString* getUse() const {  return m_use.getValue(); }
    const QString* getUsehref() const {  return m_usehref.getValue(); }

private:
    /* properties */
    XFA_Attribute<QString> m_id;
    XFA_Attribute<QString> m_use;
    XFA_Attribute<QString> m_usehref;

};

class XFA_digestMethods : public XFA_BaseNode
{
public:
    const QString* getId() const {  return m_id.getValue(); }
    const TYPE* getType() const {  return m_type.getValue(); }
    const QString* getUse() const {  return m_use.getValue(); }
    const QString* getUsehref() const {  return m_usehref.getValue(); }

private:
    /* properties */
    XFA_Attribute<QString> m_id;
    XFA_Attribute<TYPE> m_type;
    XFA_Attribute<QString> m_use;
    XFA_Attribute<QString> m_usehref;

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

private:
    /* properties */
    XFA_Attribute<CAP> m_cap;
    XFA_Attribute<QString> m_id;
    XFA_Attribute<PRESENCE> m_presence;
    XFA_Attribute<STROKE> m_stroke;
    XFA_Attribute<XFA_Measurement> m_thickness;
    XFA_Attribute<QString> m_use;
    XFA_Attribute<QString> m_usehref;

};

class XFA_encoding : public XFA_BaseNode
{
public:
    const QString* getId() const {  return m_id.getValue(); }
    const QString* getUse() const {  return m_use.getValue(); }
    const QString* getUsehref() const {  return m_usehref.getValue(); }

private:
    /* properties */
    XFA_Attribute<QString> m_id;
    XFA_Attribute<QString> m_use;
    XFA_Attribute<QString> m_usehref;

};

class XFA_encodings : public XFA_BaseNode
{
public:
    const QString* getId() const {  return m_id.getValue(); }
    const TYPE* getType() const {  return m_type.getValue(); }
    const QString* getUse() const {  return m_use.getValue(); }
    const QString* getUsehref() const {  return m_usehref.getValue(); }

private:
    /* properties */
    XFA_Attribute<QString> m_id;
    XFA_Attribute<TYPE> m_type;
    XFA_Attribute<QString> m_use;
    XFA_Attribute<QString> m_usehref;

};

class XFA_encrypt : public XFA_BaseNode
{
public:
    const QString* getId() const {  return m_id.getValue(); }
    const QString* getUse() const {  return m_use.getValue(); }
    const QString* getUsehref() const {  return m_usehref.getValue(); }

private:
    /* properties */
    XFA_Attribute<QString> m_id;
    XFA_Attribute<QString> m_use;
    XFA_Attribute<QString> m_usehref;

};

class XFA_encryptData : public XFA_BaseNode
{
public:
    const QString* getId() const {  return m_id.getValue(); }
    const OPERATION* getOperation() const {  return m_operation.getValue(); }
    const QString* getTarget() const {  return m_target.getValue(); }
    const QString* getUse() const {  return m_use.getValue(); }
    const QString* getUsehref() const {  return m_usehref.getValue(); }

private:
    /* properties */
    XFA_Attribute<QString> m_id;
    XFA_Attribute<OPERATION> m_operation;
    XFA_Attribute<QString> m_target;
    XFA_Attribute<QString> m_use;
    XFA_Attribute<QString> m_usehref;

};

class XFA_encryption : public XFA_BaseNode
{
public:
    const QString* getId() const {  return m_id.getValue(); }
    const TYPE* getType() const {  return m_type.getValue(); }
    const QString* getUse() const {  return m_use.getValue(); }
    const QString* getUsehref() const {  return m_usehref.getValue(); }

private:
    /* properties */
    XFA_Attribute<QString> m_id;
    XFA_Attribute<TYPE> m_type;
    XFA_Attribute<QString> m_use;
    XFA_Attribute<QString> m_usehref;

};

class XFA_encryptionMethod : public XFA_BaseNode
{
public:
    const QString* getId() const {  return m_id.getValue(); }
    const QString* getUse() const {  return m_use.getValue(); }
    const QString* getUsehref() const {  return m_usehref.getValue(); }

private:
    /* properties */
    XFA_Attribute<QString> m_id;
    XFA_Attribute<QString> m_use;
    XFA_Attribute<QString> m_usehref;

};

class XFA_encryptionMethods : public XFA_BaseNode
{
public:
    const QString* getId() const {  return m_id.getValue(); }
    const TYPE* getType() const {  return m_type.getValue(); }
    const QString* getUse() const {  return m_use.getValue(); }
    const QString* getUsehref() const {  return m_usehref.getValue(); }

private:
    /* properties */
    XFA_Attribute<QString> m_id;
    XFA_Attribute<TYPE> m_type;
    XFA_Attribute<QString> m_use;
    XFA_Attribute<QString> m_usehref;

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

private:
    /* properties */
    XFA_Attribute<ACTIVITY> m_activity;
    XFA_Attribute<QString> m_id;
    XFA_Attribute<LISTEN> m_listen;
    XFA_Attribute<QString> m_name;
    XFA_Attribute<QString> m_ref;
    XFA_Attribute<QString> m_use;
    XFA_Attribute<QString> m_usehref;

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

};

class XFA_extras : public XFA_BaseNode
{
public:
    const QString* getId() const {  return m_id.getValue(); }
    const QString* getName() const {  return m_name.getValue(); }
    const QString* getUse() const {  return m_use.getValue(); }
    const QString* getUsehref() const {  return m_usehref.getValue(); }

private:
    /* properties */
    XFA_Attribute<QString> m_id;
    XFA_Attribute<QString> m_name;
    XFA_Attribute<QString> m_use;
    XFA_Attribute<QString> m_usehref;

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

};

class XFA_fill : public XFA_BaseNode
{
public:
    const QString* getId() const {  return m_id.getValue(); }
    const PRESENCE* getPresence() const {  return m_presence.getValue(); }
    const QString* getUse() const {  return m_use.getValue(); }
    const QString* getUsehref() const {  return m_usehref.getValue(); }

private:
    /* properties */
    XFA_Attribute<QString> m_id;
    XFA_Attribute<PRESENCE> m_presence;
    XFA_Attribute<QString> m_use;
    XFA_Attribute<QString> m_usehref;

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

private:
    /* properties */
    XFA_Attribute<QString> m_addRevocationInfo;
    XFA_Attribute<QString> m_id;
    XFA_Attribute<QString> m_name;
    XFA_Attribute<QString> m_use;
    XFA_Attribute<QString> m_usehref;
    XFA_Attribute<QString> m_version;

};

class XFA_float : public XFA_BaseNode
{
public:
    const QString* getId() const {  return m_id.getValue(); }
    const QString* getName() const {  return m_name.getValue(); }
    const QString* getUse() const {  return m_use.getValue(); }
    const QString* getUsehref() const {  return m_usehref.getValue(); }

private:
    /* properties */
    XFA_Attribute<QString> m_id;
    XFA_Attribute<QString> m_name;
    XFA_Attribute<QString> m_use;
    XFA_Attribute<QString> m_usehref;

};

class XFA_font : public XFA_BaseNode
{
public:
    const XFA_Measurement* getBaselineShift() const {  return m_baselineShift.getValue(); }
    const QString* getFontHorizontalScale() const {  return m_fontHorizontalScale.getValue(); }
    const QString* getFontVerticalScale() const {  return m_fontVerticalScale.getValue(); }
    const QString* getId() const {  return m_id.getValue(); }
    const KERNINGMODE* getKerningMode() const {  return m_kerningMode.getValue(); }

private:
    /* properties */
    XFA_Attribute<XFA_Measurement> m_baselineShift;
    XFA_Attribute<QString> m_fontHorizontalScale;
    XFA_Attribute<QString> m_fontVerticalScale;
    XFA_Attribute<QString> m_id;
    XFA_Attribute<KERNINGMODE> m_kerningMode;

};

} // namespace xfa


/* END GENERATED CODE */

}   // namespace pdf
