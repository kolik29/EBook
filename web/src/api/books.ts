import type { Book, BookPagination } from '../types/book';

export const getBooks = async (): Promise<Book[]> => {
    try {
        const res = await fetch('/books');

        if (!res.ok) {
            throw new Error('Failed to get books');
        }

        return await res.json();
    } catch (error) {
        console.error(error);
        return [];
    }
};

export const uploadBook = async (file: File): Promise<void> => {
    try {
        const formData = new FormData();
        formData.append('file', file);

        const res = await fetch('/books/upload', {
            method: 'POST',
            body: formData,
        });

        if (!res.ok) {
            throw new Error('Failed to upload book');
        }
    } catch (error) {
        console.error(error);
    }
};

export const deleteBook = async (id: number): Promise<void> => {
    try {
        const res = await fetch(`/books/${id}`, {
            method: 'DELETE',
        });

        if (!res.ok) {
            throw new Error('Failed to delete book');
        }
    } catch (error) {
        console.error(error);
    }
};

export const updateCurrentPage = async (id: number, currentPage: number): Promise<void> => {
    try {
        const res = await fetch(`/books/${id}`, {
            method: 'PATCH',
            headers: {
                'Content-Type': 'application/json',
            },
            body: JSON.stringify({ currentPage }),
        });

        if (!res.ok) {
            throw new Error('Failed to update current page');
        }
    } catch (error) {
        console.error(error);
    }
};

export const getBookPagination = async (id: number): Promise<BookPagination | null> => {
    try {
        const res = await fetch(`/books/${id}/pagination`);

        if (!res.ok) {
            throw new Error('Failed to get book pagination');
        }

        return await res.json();
    } catch (error) {
        console.error(error);
        return null;
    }
};