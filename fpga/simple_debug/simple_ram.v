module SimpleRam(
   input wire reset_i,
   input wire clk_i,
   input wire wr_i,
   input wire rd_i,
   input wire [LOG2_SIZE - 1: 0] addr_i,
   input wire [DATA_WIDTH - 1:0] data_i,
   output wire [DATA_WIDTH - 1:0] data_o
);

   parameter LOG2_SIZE = 5;
   parameter SIZE = 2**LOG2_SIZE;
   parameter DATA_WIDTH = 16;
   reg [DATA_WIDTH - 1:0] storage[SIZE-1:0];
   reg [DATA_WIDTH - 1:0] out = 0;

   assign data_o = rd_i ? out : {DATA_WIDTH{1'bZ}};

   always @ (posedge clk_i)
   begin
      if (reset_i)
      begin
         out <= 16'h0;
      end
      else
      begin
         if (wr_i)
         begin
            storage[addr_i] <= data_i;
         end
         if (rd_i)
         begin
            out <= storage[addr_i];
         end
      end
   end

endmodule
