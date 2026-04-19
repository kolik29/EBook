export interface BookPagination {
    total: number;
    current: number;
}

export interface Book {
    id: number;
    title: string;
    author: string;
    img: string;
    active: boolean;
    page: BookPagination
}

export type BookCoverProps = {
    src?: string | null;
    alt: string;
    width?: number | string;
    height?: number | string;
}

export type BooksStore = {
    books: Book[];
    loading: boolean;
    loaded: boolean;
    query: string;
    setQuery: (query: string) => void;
    loadBooks: () => Promise<void>;
    reloadBooks: () => Promise<void>;
    selectBook: (id: number) => void;
}