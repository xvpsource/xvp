//
// Extensions to support XVP server Copyright (C) 2009-2010 Colin Dean. 
//
// This program is free software; you can redistribute it and/or modify it
// under the terms of the GNU General Public License as published by the
// Free Software Foundation; either version 2 of the License, or (at your
// option) any later version.
//
// This program is distributed in the hope that it will be useful, but
// WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
// General Public License for more details.
//

import java.awt.*;
import java.awt.event.*;

public class XvpConfirmDialog extends Dialog implements ActionListener {
    private Button okButton, cancelButton;
    private String title, message;
    public boolean ok = false;

    XvpConfirmDialog(Frame frame, String action, String vmname){

	super(frame, action == "fail" ?
	      "Error - " + vmname : "Confirm " + action + " " + vmname, true);

        add(BorderLayout.CENTER,
	    new Label(action == "fail" ?
		  "Virtual machine operation failed" :
		  "Are you sure you want to " + action + " this VM?"));

        Panel panel = new Panel();
        panel.setLayout(new FlowLayout());

	okButton = new Button("Ok");
        panel.add(okButton);
        okButton.addActionListener(this);

	if (action != "fail") {
	    cancelButton = new Button("Cancel");
	    panel.add(cancelButton);
	    cancelButton.addActionListener(this);
	}

        add(BorderLayout.SOUTH, panel);
        pack();

	Point fp = frame.getLocationOnScreen();
	Dimension fd = frame.getSize(), pd = this.getSize();
	setLocation(fp.x + (fd.width - pd.width) / 2,
		    fp.y + (fd.height - pd.height)/2);
        setVisible(true);
    }
    
    public void actionPerformed(ActionEvent event){
        if(event.getSource() == okButton)
            ok = true;
	setVisible(false);
    }

    public static boolean confirmed(VncViewer viewer, String action) {
	String vmname = viewer.rfb.desktopName;
	if (vmname.startsWith("VM Console - ")) // xvp >= 1.4.2
	    vmname = vmname.substring(13);
	else if (vmname.startsWith("XenServer Console - ")) // xvp <= 1.4.1
	    vmname = vmname.substring(20);
	XvpConfirmDialog box = new XvpConfirmDialog(viewer.vncFrame,
						    action, vmname);
	boolean result = box.ok;
	box.dispose();
	return result;
    }
}
