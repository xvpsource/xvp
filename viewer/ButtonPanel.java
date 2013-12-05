//
//  Copyright (C) 2001,2002 HorizonLive.com, Inc.  All Rights Reserved.
//  Copyright (C) 1999 AT&T Laboratories Cambridge.  All Rights Reserved.
//
//  This is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License as published by
//  the Free Software Foundation; either version 2 of the License, or
//  (at your option) any later version.
//
//  This software is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details.
//
//  You should have received a copy of the GNU General Public License
//  along with this software; if not, write to the Free Software
//  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307,
//  USA.
//

//
// Extensions to support XVP server Copyright (C) 2009-2011 Colin Dean.
// Support for Ctrl-Alt-Fn contributed by Dmitry Ketov.
//

//
// ButtonPanel class implements panel with four buttons in the
// VNCViewer desktop window.
//

import java.awt.*;
import java.awt.event.*;
import java.io.*;
import java.util.LinkedHashMap;

class ButtonPanel extends Panel implements ActionListener, ItemListener {

  VncViewer viewer;
  Button disconnectButton;
  Button optionsButton;
  // Button recordButton;
  Button clipboardButton;
  Button ctrlAltDelButton;
  Checkbox ctrlCheckbox;
  Checkbox altCheckbox;
  Choice fxChoice;
  Button refreshButton;
  Button xvpShutdownButton;
  Button xvpRebootButton;
  Button xvpResetButton;

  final static LinkedHashMap<String, Integer> keys = new LinkedHashMap<String, Integer>();

  static {
	keys.put("F1", KeyEvent.VK_F1);
	keys.put("F2", KeyEvent.VK_F2);
	keys.put("F3", KeyEvent.VK_F3);
	keys.put("F4", KeyEvent.VK_F4);
	keys.put("F5", KeyEvent.VK_F5);
	keys.put("F6", KeyEvent.VK_F6);
	keys.put("F7", KeyEvent.VK_F7);
	keys.put("F8", KeyEvent.VK_F8);
	keys.put("F9", KeyEvent.VK_F9);
	keys.put("F10", KeyEvent.VK_F10);
	keys.put("F11", KeyEvent.VK_F11);
	keys.put("F12", KeyEvent.VK_F12);
  };

  ButtonPanel(VncViewer v) {
    viewer = v;

    setLayout(new FlowLayout(FlowLayout.LEFT, 0, 0));
    disconnectButton = new Button("Disconnect");
    disconnectButton.setEnabled(false);
    add(disconnectButton);
    disconnectButton.addActionListener(this);
    optionsButton = new Button("Options");
    add(optionsButton);
    optionsButton.addActionListener(this);
    clipboardButton = new Button("Clipboard");
    clipboardButton.setEnabled(false);
    add(clipboardButton);
    clipboardButton.addActionListener(this);
    // if (viewer.rec != null) {
    //   recordButton = new Button("Record");
    //   add(recordButton);
    //   recordButton.addActionListener(this);
    // }
    ctrlCheckbox = new Checkbox("Ctrl");
    ctrlCheckbox.setEnabled(false);
    add(ctrlCheckbox);
    ctrlCheckbox.addItemListener(this);
    ctrlCheckbox.setFocusable(false);

    altCheckbox = new Checkbox("Alt");
    altCheckbox.setEnabled(false);
    add(altCheckbox);
    altCheckbox.addItemListener(this);
    altCheckbox.setFocusable(false);

    fxChoice = new Choice();
    fxChoice.add("Fx");
    for (String key: keys.keySet()) {
      fxChoice.add(key);
    }
    add(fxChoice);
    fxChoice.addItemListener(this);
    fxChoice.setFocusable(false);

    ctrlAltDelButton = new Button("Ctrl-Alt-Del");
    ctrlAltDelButton.setEnabled(false);
    add(ctrlAltDelButton);
    ctrlAltDelButton.addActionListener(this);

    refreshButton = new Button("Refresh");
    refreshButton.setEnabled(false);
    add(refreshButton);
    refreshButton.addActionListener(this);

    xvpShutdownButton = new Button("Shutdown");
    xvpShutdownButton.setEnabled(false);
    xvpShutdownButton.addActionListener(this);
    add(xvpShutdownButton);

    xvpRebootButton = new Button("Reboot");
    xvpRebootButton.setEnabled(false);
    xvpRebootButton.addActionListener(this);
    add(xvpRebootButton);

    xvpResetButton = new Button("Reset");
    xvpResetButton.setEnabled(false);
    xvpResetButton.addActionListener(this);
    add(xvpResetButton);
  }

  //
  // Enable buttons on successful connection.
  //

  public void enableButtons() {
    disconnectButton.setEnabled(true);
    clipboardButton.setEnabled(true);
    refreshButton.setEnabled(true);
  }

  //
  // Disable all buttons on disconnect.
  //

  public void disableButtonsOnDisconnect() {
    remove(disconnectButton);
    disconnectButton = new Button("Hide desktop");
    disconnectButton.setEnabled(true);
    add(disconnectButton, 0);
    disconnectButton.addActionListener(this);

    optionsButton.setEnabled(false);
    clipboardButton.setEnabled(false);
    ctrlCheckbox.setEnabled(false);
    altCheckbox.setEnabled(false);
    fxChoice.setEnabled(false);
    ctrlAltDelButton.setEnabled(false);
    refreshButton.setEnabled(false);

    enableXVPShutdown(false);
    enableXVPReboot(false);
    enableXVPReset(false);

    validate();
  }

  //
  // Enable XVP extension buttons
  //

  public void enableXVPShutdown(boolean enable) {
    xvpShutdownButton.setEnabled(enable);
  }

  public void enableXVPReboot(boolean enable) {
    xvpRebootButton.setEnabled(enable);
  }

  public void enableXVPReset(boolean enable) {
    xvpResetButton.setEnabled(enable);
  }

  //
  // Enable/disable controls that should not be available in view-only
  // mode.
  //

  public void enableRemoteAccessControls(boolean enable) {
    ctrlCheckbox.setEnabled(enable);
    altCheckbox.setEnabled(enable);
    fxChoice.setEnabled(enable);
    ctrlAltDelButton.setEnabled(enable);
    if (viewer.xvpExtensions) {
      if (viewer.xvpShutdown)
	enableXVPShutdown(enable);
      if (viewer.xvpReboot)
	enableXVPReboot(enable);
      if (viewer.xvpReset)
	enableXVPReset(enable);
    }
  }

  //
  // Event processing.
  //

  public void itemStateChanged(ItemEvent ie) {
  
    if (ie.getSource() == fxChoice) {
      if (fxChoice.getSelectedItem() == "Fx") {
        return;
      }

      int key = keys.get(fxChoice.getSelectedItem());
      int modifiers = 0;

      if (altCheckbox.getState()) {
        modifiers |= InputEvent.ALT_MASK;
      }

      if (ctrlCheckbox.getState()) {
        modifiers |= InputEvent.CTRL_MASK;
      }

      try {
        KeyEvent fxEvent =
          new KeyEvent(this, KeyEvent.KEY_PRESSED, 0, modifiers, key);
        viewer.rfb.writeKeyEvent(fxEvent);

        fxEvent = 
          new KeyEvent(this, KeyEvent.KEY_RELEASED, 0, modifiers, key);
        viewer.rfb.writeKeyEvent(fxEvent);
      } catch (IOException e) {
         e.printStackTrace();
      }

      fxChoice.select("Fx");
      ctrlCheckbox.setState(false);
      altCheckbox.setState(false);
    }
  }

  public void actionPerformed(ActionEvent evt) {

    viewer.moveFocusToDesktop();

    if (evt.getSource() == disconnectButton) {
      viewer.disconnect();

    } else if (evt.getSource() == optionsButton) {
      viewer.options.setVisible(!viewer.options.isVisible());

    // } else if (evt.getSource() == recordButton) {
    //   viewer.rec.setVisible(!viewer.rec.isVisible());

    } else if (evt.getSource() == clipboardButton) {
      viewer.clipboard.setVisible(!viewer.clipboard.isVisible());

    } else if (evt.getSource() == ctrlAltDelButton) {
      try {
        final int modifiers = InputEvent.CTRL_MASK | InputEvent.ALT_MASK;

        KeyEvent ctrlAltDelEvent =
          new KeyEvent(this, KeyEvent.KEY_PRESSED, 0, modifiers, 127);
        viewer.rfb.writeKeyEvent(ctrlAltDelEvent);

        ctrlAltDelEvent =
          new KeyEvent(this, KeyEvent.KEY_RELEASED, 0, modifiers, 127);
        viewer.rfb.writeKeyEvent(ctrlAltDelEvent);

      } catch (IOException e) {
        e.printStackTrace();
      }
    } else if (evt.getSource() == refreshButton) {
      try {
	RfbProto rfb = viewer.rfb;
	rfb.writeFramebufferUpdateRequest(0, 0, rfb.framebufferWidth,
					  rfb.framebufferHeight, false);
      } catch (IOException e) {
        e.printStackTrace();
      }
    } else if (evt.getSource() == xvpShutdownButton &&
	       XvpConfirmDialog.confirmed(viewer, "shutdown")) {
      try {
	RfbProto rfb = viewer.rfb;
	rfb.writeClientXVPCode(rfb.XVPCodeShutdown);
      } catch (IOException e) {
        e.printStackTrace();
      }
    } else if (evt.getSource() == xvpRebootButton &&
	       XvpConfirmDialog.confirmed(viewer, "reboot")) {
      try {
	RfbProto rfb = viewer.rfb;
	rfb.writeClientXVPCode(rfb.XVPCodeReboot);
      } catch (IOException e) {
        e.printStackTrace();
      }
    } else if (evt.getSource() == xvpResetButton &&
	       XvpConfirmDialog.confirmed(viewer, "reset")) {
      try {
	RfbProto rfb = viewer.rfb;
	rfb.writeClientXVPCode(rfb.XVPCodeReset);
      } catch (IOException e) {
        e.printStackTrace();
      }
    }
  }
}

