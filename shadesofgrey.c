/* shadesofgrey.c - Shades of Grey plugin for the Gimp.
 * Copyright (C) 2012 Roberto Montagna.
 *
 * The contents of this file are subject to the Common Public
 * Attribution License Version 1.0 (the “License”); you may not use this
 * file except in compliance with the License. You may obtain a copy of
 * the License at _____________. The License is based on the Mozilla
 * Public License Version 1.1 but Sections 14 and 15 have been added to
 * cover use of software over a computer network and provide for limited
 * attribution for the Original Developer. In addition, Exhibit A has been
 * modified to be consistent with Exhibit B. Software distributed under
 * the License is distributed on an “AS IS” basis, WITHOUT WARRANTY OF ANY
 * KIND, either express or implied. See the License for the specific
 * language governing rights and limitations under the License.
 *
 * The Original Code is the Shades of Grey plugin for the Gimp.
 *
 * The Initial Developer of the Original Code is Roberto Montagna. All
 * portions of the code written by Roberto Montagna are
 * Copyright (C) 2012 Roberto Montagna.
 * All Rights Reserved.
 *
 * Contributor Roberto Montagna.
 */

#include <libgimp/gimp.h>
#include <libgimp/gimpui.h>
#include <math.h>
#include <string.h>
#include <stdio.h>

#define POW2(x) ( (x)*(x) )
#define POW3(x) ( (x)*(x)*(x) )

#define CHECK_TH(pix,th) ( *(pix) <= (th) && *((pix)+1) <= th && *((pix)+2) <= th )



typedef struct {
	gint32 thresh;
	gint32 norm;
	gboolean preview;
} Shades_param;


static void query (void);
static void run   (const gchar      *name,
                   gint              nparams,
                   const GimpParam  *param,
                   gint             *nreturn_vals,
                   GimpParam       **return_vals);
static void shades_of_grey  (GimpDrawable *drawable, GimpPreview *preview);

static void im2float(guint8 *in, gfloat *out, gint32 l);
static void float2im(gfloat *in, guint8 *out, gint32 l);
static void linear2sRGB(gfloat *, gint32);
static void sRGB2linearLU(guint8 *, gfloat *, gint32);
static gfloat powN(gfloat, guint32);
static gboolean shades_dialog(GimpDrawable *);


static Shades_param par = {
	5,
	5,
	1
};

GimpPlugInInfo PLUG_IN_INFO =
{
  NULL,
  NULL,
  query,
  run
};

MAIN()

static void
query (void)
{
  static GimpParamDef args[] =
  {
    {
      GIMP_PDB_INT32,
      "run-mode",
      "Run mode"
    },
    {
      GIMP_PDB_IMAGE,
      "image",
      "Input image"
    },
    {
      GIMP_PDB_DRAWABLE,
      "drawable",
      "Input drawable"
    }
  };

  gimp_install_procedure (
    "plug-in-shadesofgrey",
    "Shades of Grey colour constancy",
    "Applies the Shades of Grey algorithm to the image",
    "Roberto Montagna",
    "Copyright Roberto Montagna",
    "2011",
    "_Shades of Grey Colour Constancy",
    "RGB*",
    GIMP_PLUGIN,
    G_N_ELEMENTS (args), 0,
    args, NULL);

  gimp_plugin_menu_register ("plug-in-shadesofgrey",
                             "<Image>/Filters/Colors");
}

static void
run (const gchar      *name,
     gint              nparams,
     const GimpParam  *param,
     gint             *nreturn_vals,
     GimpParam       **return_vals)
{
  static GimpParam  values[1];
  GimpPDBStatusType status = GIMP_PDB_SUCCESS;
  GimpRunMode       run_mode;
  GimpDrawable     *drawable;

  /* Setting mandatory output values */
  *nreturn_vals = 1;
  *return_vals  = values;

  values[0].type = GIMP_PDB_STATUS;
  values[0].data.d_status = status;

  /* Getting run_mode - we won't display a dialog if
   * we are in NONINTERACTIVE mode
   */
  run_mode = param[0].data.d_int32;

  /*  Get the specified drawable  */
  drawable = gimp_drawable_get (param[2].data.d_drawable);
  
  switch (run_mode) {
  	case GIMP_RUN_INTERACTIVE:
  		gimp_get_data("plug-in-shadesofgrey", &par);
  		
  		if (!shades_dialog(drawable))
  			return;
  		break;
  		
  	case GIMP_RUN_NONINTERACTIVE:
  		if (nparams != 4)
  			status = GIMP_PDB_CALLING_ERROR;
  		if (status == GIMP_PDB_SUCCESS)
  			par.thresh = param[3].data.d_int32;
  		break;
  	
  	case GIMP_RUN_WITH_LAST_VALS:
  		gimp_get_data("plug-in-shadesofgrey", &par);
  		break;
  	
  	default:
	  	break;
  }

  shades_of_grey (drawable, NULL);




  gimp_displays_flush ();
  gimp_drawable_detach (drawable);
  
  if (run_mode == GIMP_RUN_INTERACTIVE)
  	gimp_set_data("plug-in-shadesofgrey", &par, sizeof(Shades_param));

  return;
}

/* Pixels: row-major format, RGB. */

static void
shades_of_grey (GimpDrawable *drawable, GimpPreview *preview)
{
	gint32 i, k, channels;
	gint32 x1, y1, x2, y2;
	gint width, height;
	gint32 px1, py1, px2, py2;
	gint32 npixels, update;
	GimpPixelRgn rgn_in, rgn_out;
	guint8 *imagein, *imageout;
	gdouble ill_est[3], ill_norm;
	gfloat app_ill[3];
	gfloat *imtmp, immax;
	gfloat th_val;
	gint32 count_pix;
  
  	
  	if (!preview)
    	gimp_progress_init ("Shades of Grey...");
    
    
    
	if (preview) {
		gimp_preview_get_position (preview, &px1, &py1);
		gimp_preview_get_size (preview, &width, &height);
		px2 = px1 + width;
		py2 = py1 + height;
	}
	
	/* I need to access the whole image to estimate the illuminant for the preview. */
	gimp_drawable_mask_bounds(drawable->drawable_id,
        	                	&x1, &y1,
            	                &x2, &y2);
    
    if (!preview) {
    	width = x2-x1;
    	height = y2-y1;
    }
	
	channels = gimp_drawable_bpp (drawable->drawable_id);

	gimp_pixel_rgn_init (&rgn_in,
                       drawable,
                       x1, y1,
                       x2 - x1, y2 - y1,
                       FALSE, FALSE);
    
    if (preview)
    	gimp_pixel_rgn_init (&rgn_out,
        	               drawable,
            	           px1, py1,
                	       width, height,
                    	   TRUE, TRUE);
    else {
		gimp_pixel_rgn_init (&rgn_out,
        	               drawable,
            	           x1, y1,
                	       x2 - x1, y2 - y1,
                    	   TRUE, TRUE);
    }
	
	
	npixels = (x2-x1) * (y2-y1);
	//printf("#pixels: %d\n", npixels);
	
	/* Init memory. */
	imagein = g_new(guint8, channels * npixels);
	imtmp = g_new(gfloat, channels * npixels);
			
	ill_est[0] = 0.0;
	ill_est[1] = 0.0;
	ill_est[2] = 0.0;
	
	gimp_pixel_rgn_get_rect(&rgn_in, imagein, x1, y1, x2-x1, y2-y1);
	
	// im2double(imagein, imtmp, npixels*channels);
	
	if (!preview)
		gimp_progress_set_text("Removing gamma correction...");
	
	sRGB2linearLU(imagein, imtmp, npixels*channels);
	
	g_free(imagein);
	
	if (!preview)
		gimp_progress_set_text("Shades of Grey: estimating illuminant...");
	
	th_val = 1.0 - (gfloat)(par.thresh)/100.0;
	count_pix = 0;
	
	
	/* Handle different norms separately to speed up the computations.
	 * There's no need to use powN for powers of 2 or so. */
	if (par.norm == 0) { // max rgb
		for (i = 0; i < channels*npixels; i += channels) {
			if (CHECK_TH(&imtmp[i],th_val)) {
				for (k = 0; k < channels; k++) {
					if (imtmp[i+k] > ill_est[k])
						ill_est[k] = imtmp[i+k];
				}
			}
		}
	} else if (par.norm == 1) { // greyworld
		for (i = 0; i < channels*npixels; i += channels) {
			if (CHECK_TH(&imtmp[i],th_val)) {
				count_pix++;
				ill_est[0] += imtmp[i];
				ill_est[1] += imtmp[i+1];
				ill_est[2] += imtmp[i+2];
			}
		}
	} else if (par.norm == 2) {
		for (i = 0; i < channels*npixels; i += channels) {
			if (CHECK_TH(&imtmp[i],th_val)) {
				count_pix++;
				ill_est[0] += POW2(imtmp[i]);
				ill_est[1] += POW2(imtmp[i+1]);
				ill_est[2] += POW2(imtmp[i+2]);
			}
		}
		
		ill_est[0] = sqrt(ill_est[0]);
		ill_est[1] = sqrt(ill_est[1]);
		ill_est[2] = sqrt(ill_est[2]);
	} else if (par.norm == 3) {
		for (i = 0; i < channels*npixels; i += channels) {
			if (CHECK_TH(&imtmp[i],th_val)) {
				count_pix++;
				ill_est[0] += POW3(imtmp[i]);
				ill_est[1] += POW3(imtmp[i+1]);
				ill_est[2] += POW3(imtmp[i+2]);
			}
		}
		
		ill_est[0] = cbrt(ill_est[0]);
		ill_est[1] = cbrt(ill_est[1]);
		ill_est[2] = cbrt(ill_est[2]);
	} else if (par.norm == 4) {
		gfloat t;
		for (i = 0; i < channels*npixels; i += channels) {
			if (CHECK_TH(&imtmp[i],th_val)) {
				count_pix++;
				for (k = 0; k < channels; k++) {
					t = POW2(imtmp[i+k]);
					ill_est[k] += POW2(t);
				}
			}
		}
		
		ill_est[0] = pow(ill_est[0],0.25);
		ill_est[1] = pow(ill_est[1],0.25);
		ill_est[2] = pow(ill_est[2],0.25);
	} else if (par.norm > 4) {
		gfloat root = 1.0/par.norm;
		
		for (i = 0; i < channels*npixels; i += channels) {
			if (CHECK_TH(&imtmp[i],th_val)) {
				count_pix++;
				ill_est[0] += powN(imtmp[i],par.norm);
				ill_est[1] += powN(imtmp[i+1],par.norm);
				ill_est[2] += powN(imtmp[i+2],par.norm);
			}
		}
		
		ill_est[0] = pow(ill_est[0],root);
		ill_est[1] = pow(ill_est[1],root);
		ill_est[2] = pow(ill_est[2],root);
	}
	
	if (!preview)
		gimp_progress_update(0.33);
	
	/* Normalise illuminant. */
	if (par.norm != 0) {
		ill_est[0] /= (gdouble)count_pix;
		ill_est[1] /= (gdouble)count_pix;
		ill_est[2] /= (gdouble)count_pix;
		ill_norm = sqrt(ill_est[0]*ill_est[0] +
						ill_est[1]*ill_est[1] +
						ill_est[2]*ill_est[2]);
		
		ill_est[0] /= ill_norm;
		ill_est[1] /= ill_norm;
		ill_est[2] /= ill_norm;
	}
	
	
	//g_print("Illuminant estimated to: %g, %g, %g\n", ill_est[0], ill_est[1], ill_est[2]);
	
	immax = 0.0;
	app_ill[0] = (gfloat)ill_est[0];
	app_ill[1] = (gfloat)ill_est[1];
	app_ill[2] = (gfloat)ill_est[2];
	
	if (!preview) {
		gimp_progress_set_text("Shades of Grey: applying illuminant...");
	
		for (i = 0; i < npixels*channels; i += channels) {
			for (k = 0; k < channels; k++) {
				imtmp[i+k] /= app_ill[k];
			
				if (imtmp[i+k] > immax)
					immax = imtmp[i+k];
			}
		}
	
	
		if (immax > 1.0) {
			gimp_progress_set_text("Shades of Grey: reducing maxima...");
		
			for (i = 0; i < npixels*channels; i += channels) {
				imtmp[i] /= immax;
				imtmp[i+1] /= immax;
				imtmp[i+2] /= immax;
			}
		}
		
		gimp_progress_update(0.66);
	} else {
		g_free(imtmp);
		
		imagein = g_new(guint8, channels*width*height);
		imtmp = g_new(gfloat, channels*width*height);
		
		gimp_pixel_rgn_get_rect(&rgn_in, imagein, px1, py1, width, height);
		
		sRGB2linearLU(imagein, imtmp, channels*width*height);
		
		g_free(imagein);
		
		for (i = 0; i < width*height*channels; i += channels) {
			for (k = 0; k < channels; k++) {
				imtmp[i+k] /= app_ill[k];
			
				if (imtmp[i+k] > immax)
					immax = imtmp[i+k];
			}
		}
	
	
		if (immax > 1.0) {
			for (i = 0; i < width*height*channels; i += channels) {
				imtmp[i] /= immax;
				imtmp[i+1] /= immax;
				imtmp[i+2] /= immax;
			}
		}
	}
	
	if (!preview)
			gimp_progress_set_text("Applying gamma correction...");
	
	linear2sRGB(imtmp, width*height*channels);
		
	imageout = g_new(guint8, channels * width*height);
	
	if (!preview) {
		gimp_progress_set_text("Shades of Grey: finalising...");
		gimp_progress_update(1.0);
	}

	
	float2im(imtmp, imageout, width*height*channels);
	
	if (!preview)
		gimp_pixel_rgn_set_rect(&rgn_out, imageout, x1, y1, x2-x1, y2-y1);
	else
		gimp_pixel_rgn_set_rect(&rgn_out, imageout, px1, py1, width, height);
	
	g_free(imageout);
	g_free(imtmp);

	if (preview) {
		gimp_drawable_preview_draw_region (GIMP_DRAWABLE_PREVIEW (preview),
                                         	&rgn_out);
	} else {
		gimp_drawable_flush (drawable);
		gimp_drawable_merge_shadow (drawable->drawable_id, TRUE);
		gimp_drawable_update (drawable->drawable_id,
    	                    x1, y1,
        	                x2 - x1, y2 - y1);
    }
}

static void im2float(guint8 *in, gfloat *out, gint32 l) {
	gint32 i;
	gfloat lookup[256];
	
	for (i = 0; i < 256; ++i)
		lookup[i] = i / 255.0;
	
	for (i = 0; i < l; ++i)
		out[i] = lookup[in[i]];

}

static void float2im(gfloat *in, guint8 *out, gint32 l) {
	/* I assume the input to be in the range [0,1]. I clip everything
	 * outside that range. */
	 
	gint32 i;
	gfloat t;
	
	for (i = 0; i < l; ++i) {
		t = MIN(MAX(in[i], 0.0f), 1.0f);
		out[i] = (guint8)roundf(t*255.0);
	}
}


static void linear2sRGB(gfloat *linrgb, gint32 len) {
	/* Apply sRGB gamma correction to linear image. */
	gfloat g = 1 / 2.4;
	gint32 i;
	
	for (i = len-1; i >= 0; --i) {
		if (linrgb[i] <= 0.00304)
			linrgb[i] *= 12.92;
		else
			linrgb[i] = powf(linrgb[i]*1.055,g)-0.055;
		
		linrgb[i] = (linrgb[i] > 0.0) ? linrgb[i] : 0;
		linrgb[i] = (linrgb[i] < 1.0) ? linrgb[i] : 1.0;
	}
}


static void sRGB2linearLU(guint8 *srgb, gfloat *lin, gint32 len) {
	/* Given a 8-bit integer gamma-corrected image, converts it into a
	 * floating point linear image using a lookup table. */
	gint32 i;
	gfloat lookup[256];
	gfloat t;
	
	for (i = 255; i >= 0; --i) {
		t = (gfloat)i/255.0;
		
		if (t <= 0.03928)
			lookup[i] = t / 12.92;
		else
			lookup[i] = powf((t+0.055)/1.055,2.4);
	}
	
	for (i = len-1; i >= 0; --i) {
		lin[i] = lookup[srgb[i]];
	}
}

static gfloat powN(gfloat x, guint32 p) {
	gfloat y = 1.0;
	
	while (p > 0) {
		if (p & 1)
			y *= x;
		
		x *= x;
		p >>= 1;
	}
	
	return y;
}




static gboolean shades_dialog (GimpDrawable *drawable) {
	GtkWidget *dialog;
	GtkWidget *main_vbox;
	GtkWidget *main_hbox;
	GtkWidget *preview;
	GtkWidget *frame;
	GtkWidget *radius_label;
	GtkWidget *alignment;
	GtkWidget *spinbutton;
	GtkObject *spinbutton_adj;
	GtkWidget *spinbutton2;
	GtkObject *spinbutton2_adj;
	GtkWidget *frame_label;
	gboolean   run;
	
	gimp_ui_init ("shadesofgrey", FALSE);
	
	dialog = gimp_dialog_new ("Shades of Grey colour constancy", "shadesofgrey",
							NULL, 0,
							gimp_standard_help_func, "plug-in-shadesofgrey",
	
							GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
							GTK_STOCK_OK,     GTK_RESPONSE_OK,
	
							NULL);
	
	main_vbox = gtk_vbox_new (FALSE, 6);
	gtk_container_add (GTK_CONTAINER (GTK_DIALOG (dialog)->vbox), main_vbox);
	gtk_widget_show (main_vbox);
	
	preview = gimp_drawable_preview_new (drawable, &par.preview);
	gtk_box_pack_start (GTK_BOX (main_vbox), preview, TRUE, TRUE, 0);
	gtk_widget_show (preview);
	
	frame = gtk_frame_new (NULL);
	gtk_widget_show (frame);
	gtk_box_pack_start (GTK_BOX (main_vbox), frame, TRUE, TRUE, 0);
	gtk_container_set_border_width (GTK_CONTAINER (frame), 6);
	
	alignment = gtk_alignment_new (0.5, 0.5, 1, 1);
	gtk_widget_show (alignment);
	gtk_container_add (GTK_CONTAINER (frame), alignment);
	gtk_alignment_set_padding (GTK_ALIGNMENT (alignment), 6, 6, 6, 6);
	
	main_hbox = gtk_hbox_new (FALSE, 0);
	gtk_widget_show (main_hbox);
	gtk_container_add (GTK_CONTAINER (alignment), main_hbox);
	
	radius_label = gtk_label_new_with_mnemonic ("_Threshold (%)");
	gtk_widget_show (radius_label);
	gtk_box_pack_start (GTK_BOX (main_hbox), radius_label, FALSE, FALSE, 6);
	gtk_label_set_justify (GTK_LABEL (radius_label), GTK_JUSTIFY_RIGHT);
	
	spinbutton = gimp_spin_button_new (&spinbutton_adj, par.thresh, 
									 0, 50, 1, 1, 1, 5, 0);
	
	gtk_box_pack_start (GTK_BOX (main_hbox), spinbutton, FALSE, FALSE, 0);
	gtk_widget_show (spinbutton);
									 
	radius_label = gtk_label_new_with_mnemonic ("_Norm");
	gtk_widget_show (radius_label);
	gtk_box_pack_start (GTK_BOX (main_hbox), radius_label, FALSE, FALSE, 6);
	gtk_label_set_justify (GTK_LABEL (radius_label), GTK_JUSTIFY_RIGHT);
	
	spinbutton2 = gimp_spin_button_new (&spinbutton2_adj, par.norm, 
									 0, 16, 1, 1, 1, 5, 0);
									 
	gtk_box_pack_start (GTK_BOX (main_hbox), spinbutton2, FALSE, FALSE, 0);
	gtk_widget_show (spinbutton2);
	
	frame_label = gtk_label_new ("<b>Modify parameters</b>");
	gtk_widget_show (frame_label);
	gtk_frame_set_label_widget (GTK_FRAME (frame), frame_label);
	gtk_label_set_use_markup (GTK_LABEL (frame_label), TRUE);
	
	g_signal_connect_swapped (preview, "invalidated",
							G_CALLBACK (shades_of_grey),
							drawable);
	g_signal_connect_swapped (spinbutton_adj, "value_changed",
							G_CALLBACK (gimp_preview_invalidate),
							preview);
	g_signal_connect_swapped (spinbutton2_adj, "value_changed",
							G_CALLBACK (gimp_preview_invalidate),
							preview);
	
	shades_of_grey (drawable, GIMP_PREVIEW (preview));
	
	g_signal_connect (spinbutton_adj, "value_changed",
					G_CALLBACK (gimp_int_adjustment_update),
					&par.thresh);
	g_signal_connect (spinbutton2_adj, "value_changed",
					G_CALLBACK (gimp_int_adjustment_update),
					&par.norm);
	gtk_widget_show (dialog);
	
	run = (gimp_dialog_run (GIMP_DIALOG (dialog)) == GTK_RESPONSE_OK);
	
	gtk_widget_destroy (dialog);
	
	return run;
}

