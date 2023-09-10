import sys
import random

if len(sys.argv) not in range(2,4):
    print("Generates a hexdump of a random number of random bytes to feed other programs with this input stream")
    print(f"usage: {sys.argv[0]} max_nr_of_bytes [output_header]")
    sys.exit()
try:
    max=int(sys.argv[1])
except:
    print(f"Error: {sys.argv[1]} is not a number")
    sys.exit()
nr_of_bytes=random.randint(1,max)
random_bytes=random.randbytes(nr_of_bytes)
if len(sys.argv)==3:
    print (sys.argv[2]+" ", end="")
for random_byte in random_bytes:
    print (hex(random_byte)[2:].upper()+" ", end="")
print()