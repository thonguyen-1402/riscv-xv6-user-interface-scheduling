# A simple user-space interface program in xv6-riscv that is capable of the following tasks
Change the policy : Round Robin, Weighted Round Robin, Stride and Pass, MLFQ
Set the weight and timeslice of each process

All right reserved to the original creators : https://github.com/mit-pdos/xv6-riscv


We also implemented some testing method for the algorithms and functions mentioned above:
NOTE : THE ALGORITHMS WILL ONLY WORK IF YOU HAVE DONE : make qemu CPUS=1

To run an infinite loop for testing all algorithms and setting functions,( except the mlfq) : run quiet_worker & . From the printed pid, you can either do : schedctl weight pid <weight> or schedctl slice pid <slice>
To change the policy : schedctl policy <rr|wrr|stride|mlfq>
To run hog and interactive process test for mlfq " hog 3"   then do "chatty &"
