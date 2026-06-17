/*
 * LoadingDialog.cpp
 */
#include <wx/wxprec.h>
#ifndef WX_PRECOMP
  #include <wx/wx.h>
#endif

#include "LoadingDialog.h"

#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/gauge.h>

LoadingDialog::LoadingDialog(wxWindow * parent)
  : wxDialog(parent, wxID_ANY, wxT("Loading Client"), wxDefaultPosition, wxDefaultSize,
             wxDEFAULT_DIALOG_STYLE),
    m_message(0), m_gauge(0), m_percent(0)
{
  wxBoxSizer * top = new wxBoxSizer(wxVERTICAL);

  m_message = new wxStaticText(this, wxID_ANY, wxT("Loading..."), wxDefaultPosition,
                               wxSize(280, -1), wxALIGN_CENTRE_HORIZONTAL);
  top->Add(m_message, 0, wxEXPAND | wxALL, 12);

  wxBoxSizer * row = new wxBoxSizer(wxHORIZONTAL);
  m_gauge = new wxGauge(this, wxID_ANY, 100, wxDefaultPosition, wxSize(190, 18),
                        wxGA_HORIZONTAL | wxGA_SMOOTH);
  m_percent = new wxStaticText(this, wxID_ANY, wxT("0%"), wxDefaultPosition,
                               wxSize(44, -1), wxALIGN_CENTRE_HORIZONTAL);
  row->Add(m_gauge, 1, wxALIGN_CENTER_VERTICAL);
  row->Add(m_percent, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 8);
  top->Add(row, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 12);

  SetSizer(top);
  top->SetSizeHints(this);
  Centre();
}

void LoadingDialog::step(const wxString & message, int percent)
{
  if (percent < 0)   percent = 0;
  if (percent > 100) percent = 100;

  if (!message.IsEmpty())
    m_message->SetLabel(message);
  m_gauge->SetValue(percent);
  m_percent->SetLabel(wxString::Format(wxT("%d%%"), percent));

  // LoadWoW runs synchronously on the UI thread and blocks between steps. Refresh()+Update()
  // alone proved unreliable here (the window often only painted once, so the bar looked stuck
  // at one value), so also pump pending paint events. At startup the only other pending work
  // is the canvas repaint timer, whose handler is a trivial clear (no model loaded yet), so
  // yielding here is safe.
  Refresh();
  Update();
  wxYieldIfNeeded();
}
