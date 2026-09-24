#ifndef _X2000_JZ_MIPI_DSI_H
#define _X2000_JZ_MIPI_DSI_H

int dsi_write_cmd(struct dsi_cmd_packet *cmd_data);
int dsi_read_cmd(struct dsi_cmd_packet *cmd_data, int bytes, unsigned char *rd_buf);

#endif