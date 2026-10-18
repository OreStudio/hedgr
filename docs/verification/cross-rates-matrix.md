# Cross-rates matrix

The matrix is opened from the series browser's **Cross rates** button. It is
one `get_crm_rates` batch for the selected party, not one read per cell.

The matrix opens on the party selected in the market-data dialog, on the CRM
chosen from the picker. It shows currency flags in the headers and row labels
and the batch's UTC as-of time. Spot precision comes from ORE's currency-pair conventions: four decimals
for AUD/USD and two for JPY. Zero deltas print `0.00%` in the neutral text colour; nonzero moves print a
coloured percentage and leave a faint green or red heat tint in the cell. The footer shows the fresh-pair count and the batch
latency. The currency rows and columns are frozen; the selected cell can be
opened as an FX chart with a double click.

The deployment computes the batch on demand. The window polls every five
seconds only while it is drawn, so a hidden matrix does not keep a rate engine
busy. IR curves are not part of this matrix; they remain term structures.
