labwc-theme(5)

# NAME

labwc - theme files

# THEME

The theme engine aims to be compatible with openbox and themes will be
searched for in the following order:

- ${XDG_DATA_HOME:-$HOME/.local/share}/themes/<theme-name>/labwc/
- $HOME/.themes/<theme-name>/labwc/
- /usr/share/themes/<theme-name>/labwc/
- /usr/local/share/themes/<theme-name>/labwc/
- /opt/share/themes/<theme-name>/labwc/
- ${XDG_DATA_HOME:-$HOME/.local/share}/themes/<theme-name>/openbox-3/
- $HOME/.themes/<theme-name>/openbox-3/
- /usr/share/themes/<theme-name>/openbox-3/
- /usr/local/share/themes/<theme-name>/openbox-3/
- /opt/share/themes/<theme-name>/openbox-3/

When $XDG_DATA_HOME is defined, it replaces (rather than augments)
$HOME/.local/share. The same is the case for $XDG_DATA_DIRS and /usr/share/.

Choosing a theme is done by editing the <name> key in the <theme> section of
the rc.xml configuration file (labwc-config(5)).

A theme consists of a themerc file and optionally some titlebar icons (referred
to as buttons).

Theme settings specified in themerc can be overridden by creating a
'themerc-override' file in the configuration directory, which is normally
$HOME/.config/labwc/ but can be a few other locations as described in
labwc-config(5).

# DATA TYPES

*color*
	Colors can be specified by name or hexadecimal Red-Green-Blue (RGB)
	value:

	*color name* refers to "X11 color name", for example: 'white', 'grey40',
	'pink' and 'peach puff'. More information can be found here:
	- http://en.wikipedia.org/wiki/Web_colors#X11_color_names
	- http://en.wikipedia.org/wiki/X11_color_names
	- https://github.com/labwc/labwc/blob/master/src/common/rgb.txt

	*RGB value*

	- #rrggbb (hexadecimal RGB values)
	- #rrggbb aaa (same but with decimal alpha value percentage)
	- #rrggbbaa (same but with inline alpha value in hex encoding)
	- #rgb (hexadecimal RGB values where #fff is shorthand for #f0f0f0)
	  (since 0.9.0)

	Note: the #rrggbb aaa notation is deprecated starting from
	labwc 0.7.2 and may be removed in future releases.

*justification*
	Justification determines the horizontal alignment of text.
	Valid options are Left, Center and Right.

*textures*
	Textures determine the visual look of an element. They are specified
	through a text string with a number of fields. Capitalization is not
	significant. The format is as follows:

```
(Solid | Gradient <type>)
```

	... where *|* stands for "or".

	- *Solid* means that the background of the texture is filled with a
	  single color. The texture must be accompanied by a single color field.
	  For example:

```
window.*.title.bg: Solid
window.*.title.bg.color: #f8f5f2
```

	- *Gradient* means that the background of the texture is filled with a
	  gradient from one color to another. When Gradient is specified, it
	  must be followed by the gradient's type. It must also be accompanied
	  by the fields *color* and *colorTo*. Valid gradient types are:
	  (1) *Vertical* where the gradient goes from the top edge to the bottom
	  (2) *SplitVertical* where the gradient is split in the middle and goes
	  out toward the top and bottom edges. SplitVertical gradients have two
	  extra, optional color fields: *color.splitTo* and *colorTo.splitTo*.
	  These colors are the light colors used on the far top and bottom of
	  the SplitVertical gradient. When these are omitted, the default values
	  for these are *color x 5/4* and *colorTo x 17/16*. Examples:

```
# Vertical gradient
window.*.title.bg: Gradient Vertical
window.*.title.bg.color: #658fb5
window.*.title.bg.colorTo: #4d6982

# SplitVertical gradient
window.*.title.bg: Gradient SplitVertical
window.*.title.bg.color: #658fb5
window.*.title.bg.color.splitTo: #7595b9
window.*.title.bg.colorTo: #4d6982
window.*.title.bg.colorTo.splitTo: #557485
```

# THEME ELEMENTS

*border.width*
	Line width (integer) of border drawn around window frames.
	Default is 1.

*window.titlebar.padding.width*
	Horizontal titlebar padding size, in pixels, between border and first
	button on the left/right.
	Default is 0.

*window.titlebar.padding.height*
	Vertical titlebar padding size, in pixels.
	Default is 0.

*window.active.border.color*
	Border color of active window. Default is #aaaaaa.

*window.inactive.border.color*
	Border color of inactive window. Default is #aaaaaa.

*window.active.indicator.toggled-keybind.color*
	Status indicator for the ToggleKeybinds action. Can be set to the same
	value as set for window.active.border.color to disable the status
	indicator. Default is #ff0000.

*window.active.title.bg*
	Texture for the focused window's titlebar. See texture section above.
	Default is *Solid*

*window.active.title.bg.color*
	Background color for the focused window's titlebar. See texture section
	for additional color fields associated with gradients.
	Default is #e1dedb.

*window.inactive.title.bg*
	Texture for non-focused windows' titlebars. See texture section above.
	Default is *Solid*

*window.inactive.title.bg.color*
	Background color for non-focused windows' titlebars. See texture section
	for additional color fields associated with gradients.
	Default is #f6f5f4.

*window.active.label.text.color*
	Text color for the focused window's titlebar. Default is #000000.

*window.inactive.label.text.color*
	Text color non-focused windows' titlebars. Default is #000000.

*window.label.text.justify*
	Specifies how window titles are aligned in the titlebar for both
	focused and unfocused windows. Type justification. Default is Center.

*window.button.width*
	Width of a titlebar button, in pixels.
	Default is 26.

*window.button.height*
	Height of a titlebar button, in pixels.
	Default is 26.

*window.button.spacing*
	Space between titlebar buttons, in pixels.
	Default is 0.

*window.button.hover.bg.corner-radius*
	Radius of the hover effect of a titlebar button, in pixels.
	Default is 0.

	Note: for a circular hover effect, set *window.button.width* and
	*window.button.height* equal and *window.button.hover.bg.corner-radius*
	half of them.

*window.active.button.unpressed.image.color*
	Color of the images in titlebar buttons in their default, unpressed,
	state. This element is for the focused window. Default is #000000.

*window.inactive.button.unpressed.image.color*
	Color of the images in titlebar buttons in their default, unpressed,
	state. This element is for non-focused windows. Default is #000000.

Note: Button elements (i.e. window.[in]active.button.\*) may be defined uniquely
for each type of button ("menu", "iconify", "max", "shade", "desk" or "close")
by appending the type after the "button" node. For example:

- window.active.button.iconify.unpressed.image.color indicates the color of the
iconify button for active windows; while

- window.inactive.button.close.unpressed.image.color indicates the color of the
close button for inactive windows.

This syntax is not documented by Openbox, but is supported and is used by many
popular themes. For the sake of brevity, these elements are not listed here, but
all are supported.

*window.active.shadow.size*
	Size of the drop-shadow for the focused window, in pixels.
	Default is 60.

*window.inactive.shadow.size*
	Size of drop-shadows for non-focused windows, in pixels.
	Default is 40.

*window.active.shadow.color*
	Color of the drop-shadow for the focused window, including opacity.
	Default is #00000060 (black with 38% opacity).

*window.inactive.shadow.color*
	Color of drop-shadows for non-focused windows, including opacity.
	Default is #00000040 (black with 25% opacity).

*menu.overlap.x*
	Horizontal overlap in pixels between submenus and their parents. A
	positive value move submenus over the top of their parents, whereas a
	negative value creates a gap between submenus and their parents.
	Default is 0.

*menu.overlap.y*
	Vertical offset in pixels between submenus and their parents. Positive
	values for downwards and negative for upwards. Default is 0.

*menu.width.min*
	Minimal width for menus. Default is 20.
	A fixed width can be achieved by setting .min and .max to the same
	value.

*menu.width.max*
	Maximal width for menus. Default is 200.
	A fixed width can be achieved by setting .min and .max to the same
	value.

*menu.border.width*
	Border width of menus in pixels. Inherits *border.width* if not set.

*menu.border.color*
	Border color of menus. Inherits *window.active.border.color* if not set.

*menu.items.padding.x*
	Horizontal padding of menu text entries in pixels.
	Default is 7.

*menu.items.padding.y*
	Vertical padding of menu text entries in pixels.
	Default is 4.

*menu.items.bg.color*
	Background color of inactive menu items. Default is #fcfbfa.

*menu.items.text.color*
	Text color of inactive menu item. Default is #000000.

*menu.items.active.bg.color*
	Background color of active menu items. Default is #e1dedb.

*menu.items.active.text.color*
	Text color of active menu item. Default is #000000.

*menu.separator.width*
	Line thickness of menu separators. Default is 1.

*menu.separator.padding.width*
	Space on the left and right side of each separator line. Default is 6.

*menu.separator.padding.height*
	Space above and below each separator line. Default is 3.

*menu.separator.color*
	Menu separator color. Default is #888888.

*menu.title.bg.color*
	Menu title color. Default is #589bda.
	Note: A menu title is a separator with a label.

*menu.title.text.justify*
	Specifies how menu titles are aligned in the titlebar.
	Type justification. Default is Center.

*menu.title.text.color*
	Text color of separator label. Default is #ffffff.

*osd.bg.color*
	Background color of on-screen-display. Inherits
	*window.active.title.bg.color* if not set.

*osd.border.color*
	Border color of on-screen-display. Inherits *osd.label.text.color* if
	not set.

*osd.border.width*
	Border width of on-screen-display. Inherits *border.width* if not set.

*osd.label.text.color*
	Text color of on-screen-display. Inherits
	*window.active.label.text.color* if not set.

*osd.window-switcher.width*
	Width of window switcher in pixels. Default is 600.
	Width can also be percent of the width of the monitor.
	% is mandatory as last character in this case, max 100%

*osd.window-switcher.padding*
	Padding of window switcher in pixels. This is the space between the
	window-switcher border and its items. Default is 4.

*osd.window-switcher.item.padding.x*
	Horizontal padding of window switcher entries in pixels.
	Default is 10.

*osd.window-switcher.item.padding.y*
	Vertical padding of window switcher entries in pixels.
	Default is 1.

*osd.window-switcher.item.active.border.width*
	Border width of the selection box in the window switcher in pixels.
	Default is 2.

*osd.window-switcher.item.icon.size*
	Size of the icon in window switcher, in pixels.
	If not set, the font size derived from <theme><font place="OnScreenDisplay">
	is used.

*osd.window-switcher.preview.border.width*
	Border width of the outlines shown as the preview of the window selected
	by window switcher. Inherits *osd.border.width* if not set.

*osd.window-switcher.preview.border.color*
	Color(s) of the outlines shown as the preview of the window selected by
	window switcher. Possible value is a color or up to 3 colors separated
	by commas (e.g. "#ffffff,#000000,#ffffff"). When multiple colors are
	specified, a multi-line rectangle with each line having the specified
	color is drawn. If not set, this inherits the on-screen-display theme
	("[*osd.bg.color*],[*osd.label.text.color*],[*osd.bg.color*]").

*osd.workspace-switcher.boxes.width*
	Width of boxes in workspace switcher in pixels. Setting to 0 disables
	boxes. Default is 20.

*osd.workspace-switcher.boxes.height*
	Height of boxes in workspace switcher in pixels. Setting to 0 disables
	boxes. Default is 20.

*osd.workspace-switcher.boxes.border.width*
	Border width of boxes in workspace switcher in pixels. Default is 2.

*snapping.overlay.region.bg.enabled* [yes|no]
	Show a filled rectangle as an overlay when a window is snapped to a
	region. Default is yes for hardware-based renderers and no for
	software-based renderers.

*snapping.overlay.edge.bg.enabled* [yes|no]
	Show a filled rectangle as an overlay when a window is snapped to an
	edge. Default is yes for hardware-based renderer and no for
	software-based renderers.

*snapping.overlay.region.border.enabled* [yes|no]
	Show an outlined rectangle as an overlay when a window is snapped to a
	region. Default is no for hardware-based renderers and yes for
	software-based renderers.

*snapping.overlay.edge.border.enabled* [yes|no]
	Show an outlined rectangle as an overlay when a window is snapped to an
	edge. Default is no for hardware-based renderer and yes for
	software-based renderers.

*snapping.overlay.region.bg.color*
	Color of a filled rectangle shown as an overlay when a window is snapped
	to a region. Default is #8080b380.

*snapping.overlay.edge.bg.color*
	Color of a filled rectangle shown as an overlay when a window is snapped
	to an edge. Default is #8080b380.

*snapping.overlay.region.border.width*
	Border width of an outlined rectangle shown as an overlay when a window
	is snapped to a region. Inherits `osd.border.width` if not set.

*snapping.overlay.edge.border.width*
	Border width of an outlined rectangle shown as an overlay when a window
	is snapped to an edge. Inherits `osd.border.width` if not set.

*snapping.overlay.region.border.color*
	Color(s) of an outlined rectangle shown as an overlay when a window is
	snapped to a region. Possible values and the default value are the same
	as those of *osd.window-switcher.preview.border.color*.

*snapping.overlay.edge.border.color*
	Color(s) of an outlined rectangle shown as an overlay when a window is
	snapped to an edge. Possible values and the default value are the same
	as those of *osd.window-switcher.preview.border.color*.

*border.color*
	Set both *window.active.border.color* and
	*window.inactive.border.color*. This is obsolete, but supported for
	backward compatibility as some themes still contain it.

*magnifier.border.width*
	Width of magnifier window border in pixels. Default is 1.

*magnifier.border.color*
	Color of the magnfier window border. Default is #ff0000 (red).

# BUTTONS

The images used for the titlebar icons are referred to as buttons.

The image formats listed below are supported. They are listed in order of
precedence, where the first format in the list is searched for first.

- png
- svg
- xbm

By default, buttons are 1-bit xbm (X Bitmaps). These are masks where 0=clear and
1=colored. The xbm image files are placed in the same directory as the themerc
file within a particular theme. The following xbm buttons are supported:

- max.xbm
- iconify.xbm
- close.xbm
- menu.xbm
- max_toggled.xbm
- desk.xbm
- desk_toggled.xbm
- shade.xbm
- shade_toggled.xbm

Additional icons can be defined to be shown when the mouse pointer is hovering
over the button in question:

- max_hover.xbm
- iconify_hover.xbm
- close_hover.xbm
- menu_hover.xbm
- max_toggled_hover.xbm
- desk_hover.xbm
- desk_toggle_hover.xbm
- shade_hover.xbm
- shade_toggle_hover.xbm

One advantage of xbm buttons over other formats is that they change color based
on the theme. Other formats use the suffices "-active" and "-inactive" to align
with the respective titlebar colors. For example: "close-active.png"

For compatibility reasons, the following alternative names are supported
for xbm files:

- max_hover_toggled.xbm for max_toggled_hover.xbm

When using png or svg icons, for a full theme experience all of the
following icons should be added:

- close-active.[png|svg]
- close_hover-active.[png|svg]
- close_hover-inactive.[png|svg]
- close-inactive.[png|svg]
- iconify-active.[png|svg]
- iconify_hover-active.[png|svg]
- iconify_hover-inactive.[png|svg]
- iconify-inactive.[png|svg]
- max-active.[png|svg]
- max_hover-active.[png|svg]
- max_hover-inactive.[png|svg]
- max-inactive.[png|svg]
- max_toggled-active.[png|svg]
- max_toggled_hover-active.[png|svg]
- max_toggled_hover-inactive.[png|svg]
- max_toggled-inactive.[png|svg]
- menu-active.[png|svg]
- menu_hover-active.[png|svg]
- menu_hover-inactive.[png|svg]
- menu-inactive.[png|svg]
- shade-active.[png|svg]
- shade_hover-active.[png|svg]
- shade_hover-inactive.[png|svg]
- shade-inactive.[png|svg]
- shade_toggled-active.[png|svg]
- shade_toggled-inactive.[png|svg]
- shade_toggled_hover-active.[png|svg]
- shade_toggled_hover-inactive.[png|svg]
- desk-active.[png|svg]
- desk_hover-active.[png|svg]
- desk_hover-inactive.[png|svg]
- desk-inactive.[png|svg]
- desk_toggled-active.[png|svg]
- desk_toggled-inactive.[png|svg]
- desk_toggled_hover-active.[png|svg]
- desk_toggled_hover-inactive.[png|svg]

# DEFINITIONS

The handle is the window edge decoration at the bottom of the window.

# SEE ALSO

labwc(1), labwc-config(5), labwc-actions(5)
