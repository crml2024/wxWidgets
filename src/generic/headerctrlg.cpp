///////////////////////////////////////////////////////////////////////////////
// Name:        src/generic/headerctrlg.cpp
// Purpose:     generic wxHeaderCtrl implementation
// Author:      Vadim Zeitlin
// Created:     2008-12-03
// Copyright:   (c) 2008 Vadim Zeitlin <vadim@wxwidgets.org>
// Licence:     wxWindows licence
///////////////////////////////////////////////////////////////////////////////

// ============================================================================
// declarations
// ============================================================================

// ----------------------------------------------------------------------------
// headers
// ----------------------------------------------------------------------------

// for compilers that support precompilation, includes "wx.h".
#include "wx/wxprec.h"


#if wxUSE_HEADERCTRL

#include "wx/headerctrl.h"

#ifdef wxHAS_GENERIC_HEADERCTRL

#include "wx/dcbuffer.h"
#include "wx/renderer.h"

// ----------------------------------------------------------------------------
// constants
// ----------------------------------------------------------------------------

namespace
{

const unsigned COL_NONE = (unsigned)-1;

} // anonymous namespace

// ============================================================================
// wxHeaderCtrl implementation
// ============================================================================

// ----------------------------------------------------------------------------
// wxHeaderCtrl creation
// ----------------------------------------------------------------------------

void wxHeaderCtrl::Init()
{
    m_numColumns = 0;
    m_hover =
    m_colBeingResized =
    m_colBeingReordered = COL_NONE;
    m_dragOffset = 0;
    m_scrollOffset = 0;
    m_wasSeparatorDClick = false;
}

bool wxHeaderCtrl::Create(wxWindow *parent,
                          wxWindowID id,
                          const wxPoint& pos,
                          const wxSize& size,
                          long style,
                          const wxString& name)
{
    if ( !wxHeaderCtrlBase::Create(parent, id, pos, size,
                                   style, wxDefaultValidator, name) )
        return false;

    // tell the system to not paint the background at all to avoid flicker as
    // we paint the entire window area in our OnPaint()
    SetBackgroundStyle(wxBG_STYLE_PAINT);

    return true;
}

wxHeaderCtrl::~wxHeaderCtrl()
{
}

// ----------------------------------------------------------------------------
// wxHeaderCtrl columns manipulation
// ----------------------------------------------------------------------------

void wxHeaderCtrl::DoSetCount(unsigned int count)
{
    // update the column indices order array before changing m_numColumns
    DoResizeColumnIndices(m_colIndices, count);

    m_numColumns = count;

    // don't leave the column index invalid, this would cause a crash later if
    // it is used from OnMouse()
    if ( m_hover >= count )
        m_hover = COL_NONE;

    InvalidateBestSize();
    Refresh();
}

unsigned int wxHeaderCtrl::DoGetCount() const
{
    return m_numColumns;
}

void wxHeaderCtrl::DoUpdate(unsigned int idx)
{
    InvalidateBestSize();

    // we need to refresh not only this column but also the ones after it in
    // case it was shown or hidden or its width changed -- it would be nice to
    // avoid doing this unnecessary by storing the old column width (TODO)
    RefreshColsAfter(idx);
}

// ----------------------------------------------------------------------------
// wxHeaderCtrl scrolling
// ----------------------------------------------------------------------------

void wxHeaderCtrl::DoScrollHorz(int dx)
{
    m_scrollOffset += dx;

    // don't call our own version which calls this function!
    wxControl::ScrollWindow(dx, 0);
}

// ----------------------------------------------------------------------------
// wxHeaderCtrl geometry
// ----------------------------------------------------------------------------

wxSize wxHeaderCtrl::DoGetBestSize() const
{
    wxWindow *win = GetParent();
    int height = wxRendererNative::Get().GetHeaderButtonHeight( win );

    // the vertical size is rather arbitrary but it looks better if we leave
    // some space around the text
    return wxSize(IsEmpty() ? wxHeaderCtrlBase::DoGetBestSize().x
                            : GetColEnd(GetColumnCount() - 1),
                  height); // (7*GetCharHeight())/4);
}

int wxHeaderCtrl::GetColStart(unsigned int idx) const
{
    int pos = m_scrollOffset;
    for ( unsigned n = 0; ; n++ )
    {
        const unsigned i = m_colIndices[n];
        if ( i == idx )
            break;

        const wxHeaderColumn& col = GetColumn(i);
        if ( col.IsShown() )
            pos += col.GetWidth();
    }
	
    return pos;
}

int wxHeaderCtrl::GetColEnd(unsigned int idx) const
{
    int x = GetColStart(idx);

    return x + GetColumn(idx).GetWidth();
}

unsigned int wxHeaderCtrl::FindColumnAtPoint(int xPhysical, Region& pos_region) const
{
    int pos = 0;
    int xLogical = xPhysical - m_scrollOffset;
    const unsigned count = GetColumnCount();
    for ( unsigned n = 0; n < count; n++ )
    {
        const unsigned idx = m_colIndices[n];
        const wxHeaderColumn& col = GetColumn(idx);
        if ( col.IsHidden() )
            continue;
		
		const auto last_col_end = pos;
        pos += col.GetWidth();

        // TODO: don't hardcode sensitivity
        const int separatorClickMargin = FromDIP(8);

        // if the column is resizable, check if we're approximatively over the
        // line separating it from the next column
        if ( col.IsResizeable() && abs(xLogical - pos) < separatorClickMargin )
        {
			pos_region = Region::Separator;
            return idx;
        }

        // inside this column?
        if ( xLogical < pos && xLogical >= last_col_end)
        {
            if ( xLogical - last_col_end < pos - xLogical)
                pos_region = Region::LeftHalf;
			else
				pos_region = Region::RightHalf;
            return idx;
        }
    }

    pos_region = Region::NoWhere;
    return COL_NONE;
}

unsigned int wxHeaderCtrl::FindColumnClosestToPoint(int xPhysical, Region& pos_region) const
{
    const unsigned int colIndexAtPoint = FindColumnAtPoint(xPhysical, pos_region);
	
    // valid column found?
    if ( colIndexAtPoint != COL_NONE )
        return colIndexAtPoint;
	
	pos_region = Region::NoWhere;
    // if not, xPhysical must be beyond the rightmost column, so return its
    // index instead -- if we have it
    const unsigned int count = GetColumnCount();
    if ( !count )
        return COL_NONE;

    return m_colIndices[count - 1];
}

unsigned int wxHeaderCtrl::FindColumnAfter(const unsigned int column_idx) const{
    const unsigned count = GetColumnCount();
    auto after_idx = COL_NONE;
    //auto target_column_found = false;
    for ( unsigned n = 0; n < count; n++ )
    {
        if (m_colIndices[n] == column_idx && n + 1 < count ){
            after_idx = m_colIndices[n + 1];
            break;
        }
    }
    return after_idx;
}

unsigned int wxHeaderCtrl::FindColumnBefore(const unsigned int column_idx) const{
    const unsigned count = GetColumnCount();
    auto before_idx = COL_NONE;
    auto target_column_found = false;
    for ( unsigned n = 0; n < count; n++ )
    {
        if (m_colIndices[n] == column_idx){
            target_column_found = true;
            break;
        }
        before_idx = m_colIndices[n];
    }
    if (not target_column_found)
        before_idx = COL_NONE;
    return before_idx;
}

// ----------------------------------------------------------------------------
// wxHeaderCtrl repainting
// ----------------------------------------------------------------------------

void wxHeaderCtrl::RefreshCol(unsigned int idx)
{
    wxRect rect = GetClientRect();
    rect.x += GetColStart(idx);
    rect.width = GetColumn(idx).GetWidth();

    RefreshRect(rect);
}

void wxHeaderCtrl::RefreshColIfNotNone(unsigned int idx)
{
    if ( idx != COL_NONE )
        RefreshCol(idx);
}

void wxHeaderCtrl::RefreshColsAfter(unsigned int idx)
{
    wxRect rect = GetClientRect();
    const int ofs = GetColStart(idx);
    rect.x += ofs;
    rect.width -= ofs;

    RefreshRect(rect);
}

// ----------------------------------------------------------------------------
// wxHeaderCtrl dragging/resizing/reordering
// ----------------------------------------------------------------------------

bool wxHeaderCtrl::IsResizing() const
{
    return m_colBeingResized != COL_NONE;
}

bool wxHeaderCtrl::IsReordering() const
{
    return m_colBeingReordered != COL_NONE;
}

void wxHeaderCtrl::ClearMarkers()
{
    wxClientDC dc(this);

    wxDCOverlay dcover(m_overlay, &dc);
    dcover.Clear();
}

void wxHeaderCtrl::EndDragging()
{
    // We currently only use markers for reordering, not for resizing
    if (IsReordering())
    {
        ClearMarkers();
        m_overlay.Reset();
    }

    // don't use the special dragging cursor any more
    SetCursor(wxNullCursor);
}

void wxHeaderCtrl::CancelDragging()
{
    wxASSERT_MSG( IsDragging(),
                  "shouldn't be called if we're not dragging anything" );

    EndDragging();

    unsigned int& col = IsResizing() ? m_colBeingResized : m_colBeingReordered;

    wxHeaderCtrlEvent event(wxEVT_HEADER_DRAGGING_CANCELLED, GetId());
    event.SetEventObject(this);
    event.SetColumn(col);

    GetEventHandler()->ProcessEvent(event);

    col = COL_NONE;
}

int wxHeaderCtrl::ConstrainByMinWidth(unsigned int col, int& xPhysical)
{
    const int xStart = GetColStart(col);

    // notice that GetMinWidth() returns 0 if there is no minimal width so it
    // still makes sense to use it even in this case
    const int xMinEnd = xStart + GetColumn(col).GetMinWidth();

    if ( xPhysical < xMinEnd )
        xPhysical = xMinEnd;

    return xPhysical - xStart;
}

void wxHeaderCtrl::StartOrContinueResizing(unsigned int col, int xPhysical)
{
    wxHeaderCtrlEvent event(IsResizing() ? wxEVT_HEADER_RESIZING
                                         : wxEVT_HEADER_BEGIN_RESIZE,
                            GetId());
    event.SetEventObject(this);
    event.SetColumn(col);

    event.SetWidth(ConstrainByMinWidth(col, xPhysical));

    if ( GetEventHandler()->ProcessEvent(event) && !event.IsAllowed() )
    {
        if ( IsResizing() )
        {
            ReleaseMouse();
            CancelDragging();
        }
        //else: nothing to do -- we just don't start to resize
    }
    else // go ahead with resizing
    {
        if ( !IsResizing() )
        {
            m_colBeingResized = col;
            SetCursor(wxCursor(wxCURSOR_SIZEWE));
            CaptureMouse();
        }
        //else: we had already done the above when we started

    }
    RefreshColsAfter(col);
}

void wxHeaderCtrl::EndResizing(int xPhysical)
{
    wxASSERT_MSG( IsResizing(), "shouldn't be called if we're not resizing" );

    EndDragging();

    ReleaseMouse();

    wxHeaderCtrlEvent event(wxEVT_HEADER_END_RESIZE, GetId());
    event.SetEventObject(this);
    event.SetColumn(m_colBeingResized);
    event.SetWidth(ConstrainByMinWidth(m_colBeingResized, xPhysical));

    GetEventHandler()->ProcessEvent(event);

    m_colBeingResized = COL_NONE;
}

void wxHeaderCtrl::UpdateReorderingMarker(int xPhysical)
{
    wxClientDC dc(this);

    wxDCOverlay dcover(m_overlay, &dc);
    dcover.Clear();

    dc.SetPen(*wxBLUE);
    dc.SetBrush(*wxTRANSPARENT_BRUSH);

    // draw the phantom position of the column being dragged
    int x = xPhysical - m_dragOffset;
    int y = GetClientSize().y;
    dc.DrawRectangle(x, 0,
                     GetColumn(m_colBeingReordered).GetWidth(), y);

    // and also a hint indicating where it is going to be inserted if it's
    // dropped now
	auto hover_region = Region::NoWhere;
    unsigned int col = FindColumnClosestToPoint(xPhysical, hover_region);
    if ( col != COL_NONE )
    {
        static const int DROP_MARKER_WIDTH = 4;

        dc.SetBrush(*wxBLUE);
		if (hover_region == Region::LeftHalf){
            dc.DrawRectangle(GetColStart(col) - DROP_MARKER_WIDTH/2, 0,
                        DROP_MARKER_WIDTH, y);
		}
		else if (hover_region != Region::NoWhere){
			dc.DrawRectangle(GetColEnd(col) - DROP_MARKER_WIDTH/2, 0,
							 DROP_MARKER_WIDTH, y);
        }
    }
}

void wxHeaderCtrl::StartReordering(unsigned int col, int xPhysical)
{
    wxHeaderCtrlEvent event(wxEVT_HEADER_BEGIN_REORDER, GetId());
    event.SetEventObject(this);
    event.SetColumn(col);

    if ( GetEventHandler()->ProcessEvent(event) && !event.IsAllowed() )
    {
        // don't start dragging it, nothing to do otherwise
        return;
    }

    m_dragOffset = xPhysical - GetColStart(col);

    m_colBeingReordered = col;
    SetCursor(wxCursor(wxCURSOR_HAND));
    CaptureMouse();

    // do not call UpdateReorderingMarker() here: we don't want to give
    // feedback for reordering until the user starts to really move the mouse
    // as he might want to just click on the column and not move it at all
}

bool wxHeaderCtrl::EndReordering(int xPhysical)
{
    wxASSERT_MSG( IsReordering(), "shouldn't be called if we're not reordering" );

    EndDragging();

    ReleaseMouse();

    const int colOld = m_colBeingReordered;
	auto dropped_region = Region::NoWhere;
    unsigned colNew = FindColumnClosestToPoint(xPhysical, dropped_region);

    m_colBeingReordered = COL_NONE;
    auto reg_str = std::string{};
    switch(dropped_region){
    case Region::NoWhere:
        reg_str = "NoWhere";
        break;
    case Region::LeftHalf:
        reg_str = "LeftHalf";
        break;
    case Region::RightHalf:
        reg_str = "RightHalf";
        break;
    case Region::Separator:
        reg_str = "Separator";
        break;
    }
    //printf("Dropped to col %d, region : %s\n", colNew, reg_str.c_str());
	// The actual dropped pos should not simply be colNew, it should also depends on
	// which region the user dropped in.
	// if the user dropped the col on the RightHalf, the colNew should the one next to it on the right.
    auto located_by_previous_col = false;
    if ((dropped_region == Region::RightHalf || dropped_region == Region::Separator) && colNew != COL_NONE){
        //printf("Looking for the next column pos to inserted for col %d\n", colNew);
        auto nextColumn = FindColumnAfter(colNew);
        if (nextColumn != COL_NONE){
            //printf("Next col for col %d is %d\n", colNew, nextColumn);
            colNew = nextColumn;
            located_by_previous_col = true;
        }
    }
    // mouse drag must be longer than min distance m_dragOffset
    if ( xPhysical - GetColStart(colOld) == m_dragOffset )
    {
        return false;
    }

    // cannot proceed without a valid column index
    if ( colNew == COL_NONE )
    {
        return false;
    }
    if ( static_cast<int>(colNew) != colOld && dropped_region != Region::NoWhere )
    {
        wxHeaderCtrlEvent event(wxEVT_HEADER_END_REORDER, GetId());
        event.SetEventObject(this);
        event.SetColumn(colOld);
        auto new_pos = GetColumnPos(colNew);
        auto old_pos = GetColumnPos(colOld);
		// when the user drag one col from left-to-right(i.e. from low pos to higher one),
		// the actual pos to dropped should be the one just before colNew, i.e. the one on the left hand side of colNew.
        auto move_left = false;
        if (old_pos < new_pos) {
			// the last column is a bit special, we should consider it differently.
			if (new_pos != GetColumnCount() - 1 || located_by_previous_col || dropped_region == Region::LeftHalf){
                colNew = FindColumnBefore(colNew);
                assert(colNew != COL_NONE);
                new_pos = GetColumnPos(colNew);
            }
            move_left = true;
        }
		// Simulate the reorder before we actually accept it.
		// Why???
		// ToDo: better code comments to explain the logic, a diagram should be better
        auto new_colIndices = m_colIndices;
        MoveColumnInOrderArray(new_colIndices, colOld, new_pos);
        auto old_after_pos = new_colIndices.Index(colNew);
        auto new_after_pos = new_colIndices.Index(colOld);
        if (old_after_pos != old_pos || new_after_pos != new_pos || move_left || old_pos > new_pos){
            event.SetNewOrder(new_pos);
            
            //printf("Move col %d to %d, pos to %d\n", colOld, colNew, new_pos);
            if ( !GetEventHandler()->ProcessEvent(event) || event.IsAllowed() )
            {
                // do reorder the columns
                DoMoveCol(colOld, new_pos);
            }
        }
    }
    //else
        //printf("ColNew and ColOld the same, do not reordering.\n");

    // whether we moved the column or not, the user did move the mouse and so
    // did try to do it so return true
    return true;
}

// ----------------------------------------------------------------------------
// wxHeaderCtrl column reordering
// ----------------------------------------------------------------------------

void wxHeaderCtrl::DoSetColumnsOrder(const wxArrayInt& order)
{
    m_colIndices = order;
    Refresh();
}

wxArrayInt wxHeaderCtrl::DoGetColumnsOrder() const
{
    return m_colIndices;
}

void wxHeaderCtrl::DoMoveCol(unsigned int idx, unsigned int pos)
{
    MoveColumnInOrderArray(m_colIndices, idx, pos);

    Refresh();
}

// ----------------------------------------------------------------------------
// wxHeaderCtrl event handlers
// ----------------------------------------------------------------------------

wxBEGIN_EVENT_TABLE(wxHeaderCtrl, wxHeaderCtrlBase)
    EVT_PAINT(wxHeaderCtrl::OnPaint)

    EVT_MOUSE_EVENTS(wxHeaderCtrl::OnMouse)

    EVT_MOUSE_CAPTURE_LOST(wxHeaderCtrl::OnCaptureLost)

    EVT_KEY_DOWN(wxHeaderCtrl::OnKeyDown)
wxEND_EVENT_TABLE()

void wxHeaderCtrl::OnPaint(wxPaintEvent& WXUNUSED(event))
{
    int w, h;
    GetClientSize(&w, &h);

    wxAutoBufferedPaintDC dc(this);
    dc.Clear();

    // account for the horizontal scrollbar offset in the parent window
    dc.SetDeviceOrigin(m_scrollOffset, 0);

    const unsigned int count = m_numColumns;
    int xpos = 0;
    for ( unsigned int i = 0; i < count; i++ )
    {
        const unsigned idx = m_colIndices[i];
        const wxHeaderColumn& col = GetColumn(idx);
        if ( col.IsHidden() )
            continue;

        int colWidth = col.GetWidth();

        wxHeaderSortIconType sortArrow;
        if ( col.IsSortKey() )
        {
            sortArrow = col.IsSortOrderAscending() ? wxHDR_SORT_ICON_UP
                                                   : wxHDR_SORT_ICON_DOWN;
        }
        else // not sorting by this column
        {
            sortArrow = wxHDR_SORT_ICON_NONE;
        }

        int state = 0;
        if ( IsEnabled() )
        {
            if ( idx == m_hover )
                state = wxCONTROL_CURRENT;
        }
        else // disabled
        {
            state = wxCONTROL_DISABLED;
        }

        if (i == 0)
           state |= wxCONTROL_SPECIAL;

        wxHeaderButtonParams params;
        params.m_labelText = col.GetTitle();
        params.m_labelBitmap = col.GetBitmapBundle().GetBitmapFor(this);
        params.m_labelAlignment = col.GetAlignment();

#ifdef __WXGTK__
        if (i == count-1 && xpos + colWidth >= w)
        {
            state |= wxCONTROL_DIRTY;
        }
#endif

        wxRendererNative::Get().DrawHeaderButton
                                (
                                    this,
                                    dc,
                                    wxRect(xpos, 0, colWidth, h),
                                    state,
                                    sortArrow,
                                    &params
                                );

        xpos += colWidth;
    }
    if (xpos < w)
    {
        int state = wxCONTROL_DIRTY;
        if (!IsEnabled())
            state |= wxCONTROL_DISABLED;
        wxRendererNative::Get().DrawHeaderButton(
            this, dc, wxRect(xpos, 0, w - xpos, h), state);
    }
}

void wxHeaderCtrl::OnCaptureLost(wxMouseCaptureLostEvent& WXUNUSED(event))
{
    if ( IsDragging() )
        CancelDragging();
}

void wxHeaderCtrl::OnKeyDown(wxKeyEvent& event)
{
    if ( event.GetKeyCode() == WXK_ESCAPE )
    {
        if ( IsDragging() )
        {
            ReleaseMouse();
            CancelDragging();

            return;
        }
    }

    event.Skip();
}

void wxHeaderCtrl::OnMouse(wxMouseEvent& mevent)
{
    const bool wasSeparatorDClick = m_wasSeparatorDClick;
    m_wasSeparatorDClick = false;

    // do this in advance to allow simply returning if we're not interested,
    // we'll undo it if we do handle the event below
    mevent.Skip();


    // account for the control displacement
    const int xPhysical = mevent.GetX();

    // first deal with the [continuation of any] dragging operations in
    // progress
    if ( IsResizing() )
    {
        if ( mevent.LeftUp() )
            EndResizing(xPhysical);
        else // update the live separator position
            StartOrContinueResizing(m_colBeingResized, xPhysical);

        return;
    }

    if ( IsReordering() )
    {
        if ( !mevent.LeftUp() )
        {
            // update the column position
            UpdateReorderingMarker(xPhysical);

            return;
        }

        // finish reordering and continue to generate a click event below if we
        // didn't really reorder anything
        if ( EndReordering(xPhysical) )
            return;
    }


    // find if the event is over a column at all
    Region mouse_region = Region::NoWhere;
    const unsigned col = mevent.Leaving()
                            ? COL_NONE
                            : FindColumnAtPoint(xPhysical, mouse_region);


    // update the highlighted column if it changed
    if ( col != m_hover )
    {
        const unsigned hoverOld = m_hover;
        m_hover = col;

        RefreshColIfNotNone(hoverOld);
        RefreshColIfNotNone(m_hover);
    }

    // update mouse cursor as it moves around
    if ( mevent.Moving() )
    {
        SetCursor(mouse_region == Region::Separator ? wxCursor(wxCURSOR_SIZEWE) : wxNullCursor);
        return;
    }

    // all the other events only make sense when they happen over a column
    if ( col == COL_NONE )
        return;


    // enter various dragging modes on left mouse press
    if ( mevent.LeftDown() )
    {
        if ( mouse_region == Region::Separator )
        {
            // start resizing the column
            wxASSERT_MSG( !IsResizing(), "reentering column resize mode?" );
            StartOrContinueResizing(col, xPhysical);
        }
        // on column itself - both header and column must have the appropriate
        // flags to allow dragging the column
        else if ( HasFlag(wxHD_ALLOW_REORDER) && GetColumn(col).IsReorderable() )
        {

            // start dragging the column
            wxASSERT_MSG( !IsReordering(), "reentering column move mode?" );

            StartReordering(col, xPhysical);
        }

        return;
    }

    // determine the type of header event corresponding to click events
    wxEventType evtType = wxEVT_NULL;
    const bool click = mevent.ButtonUp(),
               dblclk = mevent.ButtonDClick();
    if ( click || dblclk )
    {
        switch ( mevent.GetButton() )
        {
            case wxMOUSE_BTN_LEFT:
                // treat left double clicks on separator specially
                if ( mouse_region == Region::Separator && dblclk )
                {
                    evtType = wxEVT_HEADER_SEPARATOR_DCLICK;
                    m_wasSeparatorDClick = true;
                }
                else if (!wasSeparatorDClick)
                {
                    evtType = click ? wxEVT_HEADER_CLICK
                                    : wxEVT_HEADER_DCLICK;
                }
                break;

            case wxMOUSE_BTN_RIGHT:
                evtType = click ? wxEVT_HEADER_RIGHT_CLICK
                                : wxEVT_HEADER_RIGHT_DCLICK;
                break;

            case wxMOUSE_BTN_MIDDLE:
                evtType = click ? wxEVT_HEADER_MIDDLE_CLICK
                                : wxEVT_HEADER_MIDDLE_DCLICK;
                break;

            default:
                // ignore clicks from other mouse buttons
                ;
        }
    }

    if ( evtType == wxEVT_NULL )
        return;

    wxHeaderCtrlEvent event(evtType, GetId());
    event.SetEventObject(this);
    event.SetColumn(col);

    if ( GetEventHandler()->ProcessEvent(event) )
        mevent.Skip(false);
}

#endif // wxHAS_GENERIC_HEADERCTRL

#endif // wxUSE_HEADERCTRL
