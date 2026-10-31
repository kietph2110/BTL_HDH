# Thiết kế bộ nhớ dùng chung và bộ đếm tham chiếu (Tuần 2)

Người thực hiện: Đặng Trung Kiên – 20233473

## Nội dung
| File | Mô tả |
|---|---|
| `phan_tich_thiet_ke.sh` | Phân tích mã nguồn xv6: chọn bit RSW 8 làm `PTE_SH`, xác định các điểm cần sửa trong `uvmcopy()`, `kfree()`, `kalloc()`, tính kích thước `refcnt[]` |
| `mo_phong_refcnt.c` | Mô phỏng fork/exit với trang dùng chung: so sánh khi không có refcnt (use-after-free, double-free) và thiết kế PTE_SH + refcnt |
| `so_do/1_bo_cuc_trang_nho.svg` | Bố cục trang nhớ của cha và con sau fork() |
| `so_do/2_bang_refcount.svg` | Bảng `refcnt[]` và vòng đời một trang vật lý |
| `so_do/3_luong_fork_exit.svg` | Luồng xử lý trang chung khi fork() và exit() |

## Tóm tắt thiết kế
- `PTE_SH = (1L << 8)`: bit RSW, phần cứng bỏ qua, được `PTE_FLAGS()` giữ nguyên khi fork.
- `int refcnt[(PHYSTOP - KERNBASE) / PGSIZE]` (32 768 phần tử, 128 KB) trong `kernel/kalloc.c`, khóa riêng `kref.lock`.
- `kalloc()` đặt refcnt = 1; `kfree()` giảm refcnt và chỉ trả trang về freelist khi bằng 0.
- `uvmcopy()`: trang có `PTE_SH` được ánh xạ lại cùng PA sang con và tăng refcnt thay vì sao chép.
- System call dự kiến: `void *shmalloc(int npages)`.

## Chạy
```
bash docs/memory-design/phan_tich_thiet_ke.sh
gcc -Wall -o /tmp/mo_phong_refcnt docs/memory-design/mo_phong_refcnt.c && /tmp/mo_phong_refcnt
```
