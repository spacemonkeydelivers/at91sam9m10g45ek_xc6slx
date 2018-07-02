`timescale 1ns / 1ps
`define CHIP_SELECT_LOW_TO_HIGH			2'b01 // chip select pin switches from low to high

module simple_debug(
    inout wire [15:0] data_io,		// input-output data bus
    input wire [24:0] addr_i,			// input address bus
    input wire read_i,					// input read strobe
    input wire write_i,					// input write strobe
    input wire [1:0] cs_i,				// input chip select strobe
    input wire clk_i,					// input clk signal
    input wire reset_i,					// external reset signal, if low - reset
    input wire irq_i,					// external pin to irq to fpga
    output wire irq_o,					// external pin to irq by fpga
	 output wire [4:0] leds_o			// leds out
);

	// latches to avoid metastability
	reg stage_1 = 0;
	reg stage_2 = 0;
	reg stage_3 = 0;
	
	//signal which controls tristate iobuf
	wire disable_io;
	assign disable_io = (read_i);
	wire chip_select = cs_i[0] || cs_i[1];
	
	// to deal with external io data bus
	wire [15:0] data_to_iface;
	reg [15:0] data_from_iface = 0;
	
	reg [15:0] stored_data;
	
	// state registers
	reg	[31:0] counter = 0; // internal counter reg

	assign leds_o[0] = 1;
	assign leds_o[1] = counter[0];
	assign leds_o[2] = counter[31];
	assign leds_o[3] = irq_i;
	assign leds_o[4] = stored_data[0];

   assign irq_o = 1;

	// iobuf instance
	genvar y;
	generate
	for(y = 0; y < 16; y = y + 1 ) 
	begin : iobuf_generation
		IOBUF io_y (
			.I( data_from_iface[y] ),
			.O( data_to_iface[y] ),
			.IO( data_io[y] ),
			.T ( disable_io )
		);
	end
	endgenerate

	// becomes true if chip select was switched from low to high
	wire iface_accessed = {stage_2, stage_3} == `CHIP_SELECT_LOW_TO_HIGH;
//    wire iface_accessed = !chip_select;

	always @ (posedge clk_i) 
	begin
		// external is triggered, clear inner state
		if (!reset_i)
		begin
			counter <= 0;
			stage_3 <= 0;
			stage_2 <= 0;
			stage_1 <= 0;
			data_from_iface <= 0;
		end
		else
		begin
			counter <= counter + 1;
			// store chipselect stuff into flip-flops
			stage_3 <= stage_2;
			stage_2 <= stage_1;
			stage_1 <= chip_select;
			if (iface_accessed)
			begin
				// put data from fpga
				if (!read_i)
				begin
					// skip LSB due to 16 bit data transactions
					data_from_iface <= addr_i[15:0];
//					data_from_iface <= 16'h5aa5;
//					data_from_iface <= 16'ha55a;
				end
				// get data to fpga
				if (!write_i)
				begin
					// skip LSB due to 16 bit data transactions
					stored_data <= data_to_iface;
				end
			end
		end
	end
endmodule
