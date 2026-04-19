import type { Book, BookPagination } from "../types/book";

const delay = (ms: number) => new Promise((resolve) => setTimeout(resolve, ms));

export const getBooks = async (): Promise<Book[]> => {
    let lastError: unknown;

    for (let attempt = 0; attempt < 2; attempt++) {
        try {
            const res = await fetch("/books", {
                cache: "no-store",
            });

            if (!res.ok) {
                throw new Error(`Failed to get books: ${res.status}`);
            }

            return await res.json();
        } catch (error) {
            lastError = error;

            if (attempt === 0) {
                await delay(300);
                continue;
            }
        }
    }

    throw lastError;
};

export const uploadBook = async (file: File): Promise<void> => {
    const formData = new FormData();
    formData.append("file", file);

    const res = await fetch("/books/upload", {
        method: "POST",
        body: formData,
    });

    if (!res.ok) {
        throw new Error(`Failed to upload book: ${res.status}`);
    }
};

export const deleteBook = async (id: number): Promise<void> => {
    const res = await fetch(`/books/${id}`, {
        method: "DELETE",
    });

    if (!res.ok) {
        throw new Error(`Failed to delete book: ${res.status}`);
    }
};

export const updateCurrentPage = async (id: number, currentPage: number): Promise<void> => {
    const res = await fetch(`/books/${id}`, {
        method: "PATCH",
        headers: {
            "Content-Type": "application/json",
        },
        body: JSON.stringify({ currentPage }),
    });

    if (!res.ok) {
        throw new Error(`Failed to update current page: ${res.status}`);
    }
};

export const getBookPagination = async (id: number): Promise<BookPagination> => {
    const res = await fetch(`/books/${id}/pagination`, {
        cache: "no-store",
    });

    if (!res.ok) {
        throw new Error(`Failed to get book pagination: ${res.status}`);
    }

    return await res.json();
};