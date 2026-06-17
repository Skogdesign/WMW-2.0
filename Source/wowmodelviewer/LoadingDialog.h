/*
 * LoadingDialog.h
 *
 * Small "Loading Client" progress window shown while a WoW client is being loaded
 * (CASC storage, file list, database). Driven step-by-step from ModelViewer::LoadWoW,
 * which runs synchronously on the UI thread, so step() forces an immediate repaint.
 */
#ifndef LOADINGDIALOG_H
#define LOADINGDIALOG_H

#include <wx/dialog.h>

class wxStaticText;
class wxGauge;

class LoadingDialog : public wxDialog
{
public:
  explicit LoadingDialog(wxWindow * parent);

  // Set the current stage message + percentage and repaint right away.
  void step(const wxString & message, int percent);

private:
  wxStaticText * m_message;
  wxGauge      * m_gauge;
  wxStaticText * m_percent;
};

#endif /* LOADINGDIALOG_H */
