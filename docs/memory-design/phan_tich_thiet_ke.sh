#!/bin/bash
# Phân tích mã nguồn xv6-riscv phục vụ thiết kế shared anonymous page + refcnt
# Người thực hiện: Đặng Trung Kiên - 20233473 (Tuần 2)
# Chạy tại thư mục gốc repo:  bash phan_tich_thiet_ke.sh
B='\033[1;34m'; G='\033[1;32m'; Y='\033[1;33m'; N='\033[0m'
h(){ echo; echo -e "${B}========== $1 ==========${N}"; }
ok(){ echo -e "${G}=> $1${N}"; }
[ -f kernel/vm.c ] || { echo "Hãy chạy trong thư mục gốc của repo xv6"; exit 1; }
echo -e "${Y}Máy: $(uname -srm) | Người chạy: $(whoami) | $(date '+%d/%m/%Y %H:%M')${N}"
echo -e "${Y}Repo: $(git remote get-url origin 2>/dev/null) @ $(git rev-parse --short HEAD 2>/dev/null)${N}"

h "1. Các bit cờ PTE đang dùng (kernel/riscv.h)"
grep -n "#define PTE_[VRWXU] " kernel/riscv.h
ok "Bit 0-4 đã dùng (V,R,W,X,U). Sv39 dành bit 8-9 (RSW) cho phần mềm, phần cứng bỏ qua."
ok "Chọn PTE_SH = (1L << 8) để đánh dấu trang dùng chung."
echo -n "   Kiểm tra bit 8 chưa bị dùng: "
if grep -E "#define PTE_[A-Z]+ +\(1L << [89]\)" kernel/riscv.h; then echo "ĐÃ BỊ DÙNG"; else echo "trống ✔"; fi

h "2. PTE_FLAGS giữ những bit nào?"
grep -n "define PTE_FLAGS" kernel/riscv.h
ok "Mặt nạ 0x3FF = 10 bit thấp (bit 0..9) => PTE_SH (bit 8) được giữ khi uvmcopy() chép cờ sang con."

h "3. uvmcopy() - nơi fork() sao chép bộ nhớ (kernel/vm.c)"
S=$(grep -n "^uvmcopy" kernel/vm.c | cut -d: -f1); E=$((S+30))
sed -n "${S},${E}p" kernel/vm.c | grep -n "flags = \|kalloc()\|memmove\|mappages\|uvmunmap" | sed "s/^/   dòng +/"
ok "Điểm sửa: TRƯỚC kalloc()/memmove thêm nhánh: nếu (flags & PTE_SH) thì mappages(new, i, pa) + kref_inc(pa)."

h "4. uvmunmap() giải phóng trang qua kfree() (kernel/vm.c)"
S=$(grep -n "^uvmunmap" kernel/vm.c | cut -d: -f1)
sed -n "${S},$((S+25))p" kernel/vm.c | grep -n "do_free\|kfree" | sed "s/^/   dòng +/"
ok "Không cần sửa uvmunmap(): chỉ cần kfree() biết đếm tham chiếu."

h "5. Mọi nơi gọi kfree() trong kernel"
grep -rn "kfree(" kernel/*.c | grep -v "^kernel/kalloc.c" | sed 's/^/   /'
echo "   Tổng: $(grep -rn 'kfree(' kernel/*.c | grep -v '^kernel/kalloc.c' | wc -l) lời gọi"
ok "Đặt logic refcnt BÊN TRONG kfree() => tất cả lời gọi trên tự động an toàn, không phải sửa từng chỗ."

h "6. kalloc()/kfree() hiện tại (kernel/kalloc.c)"
grep -n "^kalloc\|^kfree\|^freerange\|kmem.freelist = \|struct run" kernel/kalloc.c | sed 's/^/   /'
ok "Bản gốc không có bộ đếm: 2 lần kfree() cùng 1 trang sẽ làm hỏng freelist (double-free)."

h "7. Kích thước mảng refcnt[]"
grep -n "define KERNBASE\|define PHYSTOP" kernel/memlayout.h | sed 's/^/   /'
NP=$(( (128*1024*1024) / 4096 ))
echo "   NPHYSPAGES = (PHYSTOP - KERNBASE) / PGSIZE = 128 MB / 4 KB = $NP trang"
echo "   Bộ nhớ cho int refcnt[$NP] = $((NP*4)) byte = $((NP*4/1024)) KB  (~0.1% RAM)"
echo "   Ví dụ PA2IDX(0x87f3a000) = $(( (0x87f3a000 - 0x80000000) / 4096 ))"

h "8. Vị trí ánh xạ trang chung trong không gian ảo"
grep -n "define TRAMPOLINE\|define TRAPFRAME" kernel/memlayout.h | sed 's/^/   /'
grep -n "define MAXVA" kernel/riscv.h | sed 's/^/   /'
ok "shmalloc() ánh xạ tại PGROUNDUP(p->sz) rồi tăng p->sz => exit/exec/sbrk tự thu hồi qua uvmunmap()."

h "KẾT LUẬN THIẾT KẾ"
echo "   [1] PTE_SH = bit 8 (RSW), được PTE_FLAGS giữ nguyên khi fork"
echo "   [2] refcnt[$NP] trong kalloc.c, khóa riêng kref.lock"
echo "   [3] Sửa: kalloc() (đặt 1), kfree() (giảm, về 0 mới giải phóng), uvmcopy() (nhánh PTE_SH)"
echo "   [4] Không sửa: uvmunmap(), uvmfree(), exit/exec"
