# Cross-rates matrix

The matrix is opened from the series browser's **Cross rates** button. It is
one `get_crm_rates` batch for the selected party, not one read per cell.

The screenshot is from `clever_dijkstra` / Acme Corporation. The matrix shows
49 fresh cells from the configured CRMs, the party and CRM selectors, the
reciprocal switch, the currency filter and the batch's UTC as-of time. The
currency rows and columns are frozen; the selected cell can be opened as an FX
chart with a double click.

The deployment computes the batch on demand. The window polls every five
seconds only while it is drawn, so a hidden matrix does not keep a rate engine
busy. IR curves are not part of this matrix; they remain term structures.
